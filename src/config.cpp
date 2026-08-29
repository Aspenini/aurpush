#include "aurpush/config.hpp"

#include "aurpush/error.hpp"

#include <cstdlib>
#include <filesystem>

namespace aurpush {
namespace {

std::string strip_git_suffix(std::string s) {
  constexpr std::string_view suffix = ".git";
  if (s.size() >= suffix.size() &&
      s.compare(s.size() - suffix.size(), suffix.size(), suffix.data()) == 0) {
    s.resize(s.size() - suffix.size());
  }
  return s;
}

}  // namespace

Config config_from_env() {
  Config cfg;
  cfg.cwd = std::filesystem::current_path();
  if (const char* url = std::getenv("AURPUSH_REMOTE_URL")) {
    if (*url) {
      cfg.remote_url = url;
    }
  }
  if (const char* skip = std::getenv("AURPUSH_SKIP_SSH")) {
    cfg.skip_ssh = skip[0] != '\0' && skip[0] != '0';
  }
  return cfg;
}

std::string default_remote_url(std::string_view pkgbase) {
  if (pkgbase.empty()) {
    throw Error("cannot build AUR remote URL without pkgbase");
  }
  return "ssh://aur@aur.archlinux.org/" + std::string(pkgbase) + ".git";
}

std::string pkgbase_from_remote_url(std::string_view url) {
  std::string s(url);
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  s = strip_git_suffix(s);
  const auto slash = s.find_last_of('/');
  if (slash == std::string::npos || slash + 1 == s.size()) {
    return {};
  }
  return s.substr(slash + 1);
}

std::string remote_url_for(const Config& cfg, std::string_view pkgbase) {
  if (!cfg.remote_url.empty()) {
    return cfg.remote_url;
  }
  return default_remote_url(pkgbase);
}

}  // namespace aurpush
