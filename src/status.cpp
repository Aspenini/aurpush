#include "aurpush/commands.hpp"

#include "aurpush/colors.hpp"
#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/ssh.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <algorithm>
#include <iostream>
#include <optional>

namespace aurpush {
namespace {

struct StatusState {
  std::optional<std::string> pkgbase;
  std::optional<std::string> version;
  std::vector<std::pair<std::string, std::pair<CheckKind, std::string>>> checks;
  std::string footer;
};

void add_check(StatusState& st, const std::string& label, CheckKind kind,
               const std::string& detail) {
  st.checks.emplace_back(label, std::make_pair(kind, detail));
}

SshAuth ssh_status(const Config& cfg) {
  if (cfg.skip_ssh) {
    SshAuth auth;
    auth.ok = true;
    auth.username = "test";
    return auth;
  }
  return check_aur_ssh();
}

std::optional<GeneratedSrcinfo> try_generate(const std::filesystem::path& dir) {
  try {
    return generate_srcinfo(dir);
  } catch (const Error&) {
    return std::nullopt;
  }
}

void print_report(const StatusState& st) {
  if (st.pkgbase) {
    std::cout << "Package: " << *st.pkgbase << '\n';
    if (st.version) {
      std::cout << "Version: " << *st.version << '\n';
    }
    std::cout << '\n';
  }

  for (const auto& [label, body] : st.checks) {
    std::cout << format_check(label, body.first, body.second) << '\n';
  }
  if (!st.footer.empty()) {
    std::cout << '\n' << st.footer << '\n';
  }
}

}  // namespace

int run_status(const Config& cfg) {
  StatusState st;
  const auto& dir = cfg.cwd;
  const bool pkgbuild = file_exists(dir / "PKGBUILD");
  const auto ssh = ssh_status(cfg);

  if (!pkgbuild) {
    add_check(st, "PKGBUILD", CheckKind::Fail, "not found");
    if (ssh.ok) {
      add_check(st, "AUR SSH", CheckKind::Ok, "authenticated");
    } else {
      add_check(st, "AUR SSH", CheckKind::Fail, "not authenticated");
    }
    print_report(st);
    return 0;
  }

  add_check(st, "PKGBUILD", CheckKind::Ok, "found");

  auto generated = try_generate(dir);
  auto disk = try_parse_srcinfo_file(dir / ".SRCINFO");
  Srcinfo info;
  if (generated) {
    info = generated->parsed;
  } else if (disk) {
    info = *disk;
  }
  if (info.valid()) {
    st.pkgbase = info.pkgbase;
    st.version = info.version_string();
  }

  const bool initialized = has_marker(dir) && git::is_repo(dir);
  if (initialized) {
    add_check(st, "Workspace", CheckKind::Ok, "initialized");
  } else {
    add_check(st, "Workspace", CheckKind::Fail, "not initialized");
  }

  if (!initialized) {
    add_check(st, "AUR repository", CheckKind::Fail, "not connected");
    if (ssh.ok) {
      add_check(st, "AUR SSH", CheckKind::Ok, "authenticated");
    } else {
      add_check(st, "AUR SSH", CheckKind::Fail, "not authenticated");
    }
    st.footer = "Run `aurpush init` to initialize this workspace.";
    print_report(st);
    return 0;
  }

  const std::string url = info.valid() ? remote_url_for(cfg, info.pkgbase) : cfg.remote_url;
  const auto aur_remote = git::remote_url(dir, kAurRemote);
  bool connected = false;
  if (aur_remote && info.valid() &&
      (*aur_remote == url || is_aur_url(*aur_remote, info.pkgbase))) {
    connected = true;
  } else if (info.valid() && matching_aur_remote(dir, info.pkgbase, cfg)) {
    connected = true;
  }

  std::string remote_master;
  bool remote_query_ok = false;
  if (connected && info.valid()) {
    try {
      remote_master = git::ls_remote_master(url);
      remote_query_ok = true;
      if (remote_master.empty()) {
        add_check(st, "AUR repository", CheckKind::Warn, "does not exist yet");
      } else {
        add_check(st, "AUR repository", CheckKind::Ok, "exists");
      }
    } catch (const Error& e) {
      add_check(st, "AUR repository", CheckKind::Warn, "cannot check");
      (void)e;
    }
  } else {
    add_check(st, "AUR repository", CheckKind::Fail, "not connected");
  }

  if (ssh.ok) {
    add_check(st, "AUR SSH", CheckKind::Ok, "authenticated");
    bool push_ok = false;
    if (cfg.skip_ssh) {
      push_ok = true;
    } else if (info.valid()) {
      if (!remote_query_ok || remote_master.empty()) {
        push_ok = true;
      } else {
        const auto repos = list_aur_repos();
        push_ok = std::find(repos.begin(), repos.end(), info.pkgbase) != repos.end();
      }
    }
    if (push_ok) {
      add_check(st, "Push access", CheckKind::Ok, "available");
    } else {
      add_check(st, "Push access", CheckKind::Fail, "not available");
    }
  } else {
    add_check(st, "AUR SSH", CheckKind::Fail, "not authenticated");
  }

  if (!file_exists(dir / ".SRCINFO")) {
    add_check(st, ".SRCINFO", CheckKind::Fail, "missing");
  } else if (!generated) {
    add_check(st, ".SRCINFO", CheckKind::Warn, "cannot verify");
  } else if (normalize_text(read_file(dir / ".SRCINFO")) == generated->text) {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
  } else {
    add_check(st, ".SRCINFO", CheckKind::Warn, "outdated");
  }

  if (info.valid()) {
    const auto changes = unpublished_changes(dir, info);
    if (changes.count() == 0) {
      add_check(st, "Changes", CheckKind::Plain, "none");
    } else if (changes.count() == 1) {
      add_check(st, "Changes", CheckKind::Plain, "1 unpublished file");
    } else {
      add_check(st, "Changes", CheckKind::Plain,
                std::to_string(changes.count()) + " unpublished files");
    }
  }

  if (connected && remote_query_ok) {
    const auto local = git::rev_parse(dir, "HEAD");
    if (!local && remote_master.empty()) {
      add_check(st, "Remote", CheckKind::Ok, "up to date");
    } else if (!local && !remote_master.empty()) {
      add_check(st, "Remote", CheckKind::Warn, "behind remote");
    } else if (local && remote_master.empty()) {
      add_check(st, "Remote", CheckKind::Warn, "unpublished commits");
    } else if (local && *local == remote_master) {
      add_check(st, "Remote", CheckKind::Ok, "up to date");
    } else if (local && git::has_object(dir, remote_master)) {
      const bool local_has_remote = git::is_ancestor(dir, remote_master, *local);
      const bool remote_has_local = git::is_ancestor(dir, *local, remote_master);
      if (local_has_remote && !remote_has_local) {
        add_check(st, "Remote", CheckKind::Warn, "unpublished commits");
      } else if (remote_has_local && !local_has_remote) {
        add_check(st, "Remote", CheckKind::Fail, "behind remote");
      } else {
        add_check(st, "Remote", CheckKind::Fail, "diverged");
      }
    } else {
      add_check(st, "Remote", CheckKind::Warn, "differs from remote");
    }
  }

  print_report(st);
  return 0;
}

}  // namespace aurpush
