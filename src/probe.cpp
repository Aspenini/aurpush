#include "aurpush/probe.hpp"

#include "aurpush/error.hpp"
#include "aurpush/git.hpp"

#include <algorithm>

namespace aurpush {

SshAuth check_ssh(const Config& cfg) {
  if (cfg.skip_ssh) {
    SshAuth auth;
    auth.ok = true;
    auth.username = "test";
    return auth;
  }
  return check_aur_ssh();
}

SshAuth require_ssh(const Config& cfg) {
  auto auth = check_ssh(cfg);
  if (!auth.ok) {
    throw Error("AUR SSH authentication failed" +
                (auth.error.empty() ? "" : ": " + auth.error));
  }
  return auth;
}

void require_push_access(const Config& cfg, const std::string& pkgbase, bool remote_exists) {
  if (cfg.skip_ssh || !remote_exists) {
    return;
  }
  const auto repos = list_aur_repos();
  if (!repos.ok) {
    throw Error("could not verify push access" +
                (repos.error.empty() ? "" : ": " + repos.error));
  }
  if (std::find(repos.names.begin(), repos.names.end(), pkgbase) == repos.names.end()) {
    throw Error("no push access to " + pkgbase);
  }
}

Relation compare_heads(const std::filesystem::path& dir, const std::string& local,
                       const std::string& remote) {
  if (local == remote) {
    return Relation::Equal;
  }
  if (!git::has_object(dir, remote)) {
    return Relation::Unknown;
  }
  const bool local_has_remote = git::is_ancestor(dir, remote, local);
  const bool remote_has_local = git::is_ancestor(dir, local, remote);
  if (local_has_remote && !remote_has_local) {
    return Relation::Ahead;
  }
  if (remote_has_local && !local_has_remote) {
    return Relation::Behind;
  }
  return Relation::Diverged;
}

Relation compare_heads(const std::filesystem::path& dir,
                       const std::optional<std::string>& local,
                       const std::string& remote_master) {
  if (!local && remote_master.empty()) {
    return Relation::Equal;
  }
  if (!local) {
    return Relation::NoLocal;
  }
  if (remote_master.empty()) {
    return Relation::NoRemote;
  }
  return compare_heads(dir, *local, remote_master);
}

Probe probe_aur(const Config& cfg, std::string_view pkgbase, const std::string& url,
                const std::filesystem::path& dir) {
  Probe p;
  try {
    p.remote_master = git::ls_remote_master(url);
    p.remote_query_ok = true;
  } catch (const Error&) {
    p.remote_query_ok = false;
  }

  if (cfg.skip_ssh) {
    p.ssh.ok = true;
    p.ssh.username = "test";
    p.push_ok = true;
  } else if (p.remote_query_ok && !p.remote_master.empty()) {
    const auto repos = list_aur_repos();
    if (repos.ok) {
      p.ssh.ok = true;
      p.ssh.username = "unknown";
      p.push_ok = std::find(repos.names.begin(), repos.names.end(), std::string(pkgbase)) !=
                  repos.names.end();
    } else {
      p.push_unknown = true;
      p.ssh = check_aur_ssh();
    }
  } else {
    p.ssh = check_aur_ssh();
    p.push_ok = p.ssh.ok;
  }

  const auto local = git::is_repo(dir) ? git::rev_parse(dir, "HEAD") : std::nullopt;
  if (p.remote_query_ok) {
    p.relation = compare_heads(dir, local, p.remote_master);
  }
  return p;
}

}  // namespace aurpush
