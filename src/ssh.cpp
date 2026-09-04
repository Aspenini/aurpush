#include "aurpush/ssh.hpp"

#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <cctype>
#include <chrono>
#include <string_view>

namespace aurpush {
namespace {

// The AUR endpoint answers in well under a second; anything longer is a stall.
constexpr std::chrono::seconds kSshTimeout{20};

constexpr std::string_view kWelcome = "welcome to aur,";

ProcessResult ssh_aur(const std::string& command) {
  ProcessOptions opts;
  opts.timeout = kSshTimeout;
  return run(
      {
          "ssh",
          "-o",
          "BatchMode=yes",
          "-o",
          "ConnectTimeout=10",
          "aur@aur.archlinux.org",
          command,
      },
      opts);
}

std::string lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string failure_reason(const ProcessResult& result, const char* fallback) {
  if (result.timed_out) {
    return "timed out talking to aur.archlinux.org";
  }
  const std::string err = trim(result.err.empty() ? result.out : result.err);
  return err.empty() ? fallback : err;
}

SshAuth query_ssh() {
  SshAuth auth;
  const auto result = ssh_aur("help");
  if (!result.ok()) {
    auth.error = failure_reason(result, "SSH authentication to aur.archlinux.org failed");
    return auth;
  }
  auth.ok = true;
  auth.username = "unknown";
  // The banner is "Welcome to AUR, <user>!" -- worth parsing for the greeting,
  // but a successful exit already proves the key was accepted.
  const std::string& text = result.out.empty() ? result.err : result.out;
  for (const auto& line : split_lines(text)) {
    const std::string haystack = lower(line);
    const auto at = haystack.find(kWelcome);
    if (at == std::string::npos) {
      continue;
    }
    std::string name = trim(line.substr(at + kWelcome.size()));
    if (!name.empty() && name.back() == '!') {
      name.pop_back();
    }
    name = trim(name);
    if (!name.empty()) {
      auth.username = name;
    }
    break;
  }
  return auth;
}

AurRepos query_repos() {
  AurRepos repos;
  const auto result = ssh_aur("list-repos");
  if (!result.ok()) {
    repos.error = failure_reason(result, "failed to list AUR repositories");
    return repos;
  }
  repos.ok = true;
  for (const auto& line : split_lines(result.out)) {
    const std::string name = trim(line);
    if (!name.empty() && name[0] != '#') {
      repos.names.push_back(name);
    }
  }
  return repos;
}

}  // namespace

// Both answers are stable for the lifetime of one command, and each costs a
// network round trip, so they are probed at most once per run.
const SshAuth& check_aur_ssh() {
  static const SshAuth auth = query_ssh();
  return auth;
}

const AurRepos& list_aur_repos() {
  static const AurRepos repos = query_repos();
  return repos;
}

}  // namespace aurpush
