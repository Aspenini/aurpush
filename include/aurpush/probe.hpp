#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "aurpush/config.hpp"
#include "aurpush/ssh.hpp"

namespace aurpush {

enum class Relation { Equal, Ahead, Behind, Diverged, Unknown, NoLocal, NoRemote };

struct Probe {
  SshAuth ssh;
  std::string remote_master;
  bool remote_query_ok = false;
  bool push_ok = false;
  bool push_unknown = false;
  Relation relation = Relation::Unknown;
};

SshAuth check_ssh(const Config& cfg);
SshAuth require_ssh(const Config& cfg);
void require_push_access(const Config& cfg, const std::string& pkgbase, bool remote_exists);

Relation compare_heads(const std::filesystem::path& dir, const std::string& local,
                       const std::string& remote);
Relation compare_heads(const std::filesystem::path& dir,
                       const std::optional<std::string>& local,
                       const std::string& remote_master);

Probe probe_aur(const Config& cfg, std::string_view pkgbase, const std::string& url,
                const std::filesystem::path& dir);

}  // namespace aurpush
