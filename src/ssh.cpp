#include "aurpush/ssh.hpp"

#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <regex>

namespace aurpush {
namespace {

ProcessResult ssh_aur(const std::string& command) {
  return run({
      "ssh",
      "-o",
      "BatchMode=yes",
      "-o",
      "ConnectTimeout=10",
      "aur@aur.archlinux.org",
      command,
  });
}

}  // namespace

SshAuth check_aur_ssh() {
  SshAuth auth;
  const auto result = ssh_aur("help");
  if (!result.ok()) {
    auth.error = trim(result.err.empty() ? result.out : result.err);
    if (auth.error.empty()) {
      auth.error = "SSH authentication to aur.archlinux.org failed";
    }
    return auth;
  }
  const std::regex welcome(R"(Welcome to AUR,\s*(.+?)(?:!|\s*$))",
                           std::regex::icase);
  std::smatch match;
  const std::string text = result.out.empty() ? result.err : result.out;
  for (const auto& line : split_lines(text)) {
    if (std::regex_search(line, match, welcome)) {
      auth.ok = true;
      auth.username = trim(match[1].str());
      return auth;
    }
  }
  if (result.ok()) {
    auth.ok = true;
    auth.username = "unknown";
    return auth;
  }
  auth.error = "unexpected response from aur.archlinux.org";
  return auth;
}

AurRepos list_aur_repos() {
  AurRepos repos;
  const auto result = ssh_aur("list-repos");
  if (!result.ok()) {
    repos.error = trim(result.err.empty() ? result.out : result.err);
    if (repos.error.empty()) {
      repos.error = "failed to list AUR repositories";
    }
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

}  // namespace aurpush
