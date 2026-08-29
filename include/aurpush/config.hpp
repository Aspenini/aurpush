#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace aurpush {

struct Config {
  std::filesystem::path cwd;
  std::string remote_url;
  bool skip_ssh = false;
};

Config config_from_env();

std::string default_remote_url(std::string_view pkgbase);
std::string pkgbase_from_remote_url(std::string_view url);
std::string remote_url_for(const Config& cfg, std::string_view pkgbase);

}  // namespace aurpush
