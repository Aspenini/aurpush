#include "aurpush/commands.hpp"

#include "aurpush/colors.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/process.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace aurpush {
namespace {

struct Check {
  std::string label;
  CheckKind kind = CheckKind::Plain;
  std::string detail;
};

struct StatusState {
  std::optional<std::string> pkgbase;
  std::optional<std::string> version;
  std::vector<Check> checks;
  std::string footer;
};

void add_check(StatusState& st, std::string label, CheckKind kind, std::string detail) {
  st.checks.push_back(Check{std::move(label), kind, std::move(detail)});
}

void add_ssh_and_push(StatusState& st, const SshAuth& ssh, bool push_ok, bool push_unknown,
                      bool report_push) {
  if (!ssh.ok) {
    add_check(st, "AUR SSH", CheckKind::Fail, "not authenticated");
    return;
  }
  add_check(st, "AUR SSH", CheckKind::Ok, "authenticated");
  if (!report_push) {
    return;
  }
  if (push_unknown) {
    add_check(st, "Push access", CheckKind::Warn, "cannot check");
  } else if (push_ok) {
    add_check(st, "Push access", CheckKind::Ok, "available");
  } else {
    add_check(st, "Push access", CheckKind::Fail, "not available");
  }
}

void add_relation(StatusState& st, Relation rel) {
  switch (rel) {
    case Relation::Equal:
      add_check(st, "Remote", CheckKind::Ok, "up to date");
      break;
    case Relation::Ahead:
    case Relation::NoRemote:
      add_check(st, "Remote", CheckKind::Warn, "unpublished commits");
      break;
    case Relation::NoLocal:
      add_check(st, "Remote", CheckKind::Warn, "behind remote");
      st.footer = "Run `aurpush sync` to fast-forward.";
      break;
    case Relation::Behind:
      add_check(st, "Remote", CheckKind::Fail, "behind remote");
      st.footer = "Run `aurpush sync` to fast-forward.";
      break;
    case Relation::Diverged:
      add_check(st, "Remote", CheckKind::Fail, "diverged");
      break;
    case Relation::Unknown:
      add_check(st, "Remote", CheckKind::Warn, "differs from remote");
      st.footer = "Run `aurpush sync` to fetch the remote history.";
      break;
  }
}

std::string changes_detail(const TreeDiff& changes) {
  if (changes.empty()) {
    return "none";
  }
  std::string detail = changes.count() == 1
                           ? "1 unpublished file"
                           : std::to_string(changes.count()) + " unpublished files";
  auto append = [&](const char* kind, const std::vector<std::string>& names) {
    for (const auto& name : names) {
      detail += "\n  ";
      detail += kind;
      detail += name;
    }
  };
  append("added     ", changes.added);
  append("modified  ", changes.modified);
  append("deleted   ", changes.deleted);
  return detail;
}

// Compared by modification time on purpose: regenerating .SRCINFO means sourcing
// the PKGBUILD, and inspection must stay read-only and side-effect free.
void add_srcinfo_freshness(StatusState& st, const std::filesystem::path& dir) {
  const auto srcinfo = dir / ".SRCINFO";
  if (!file_exists(srcinfo)) {
    add_check(st, ".SRCINFO", CheckKind::Fail, "missing");
    return;
  }
  std::error_code ec;
  const auto pkg_time = std::filesystem::last_write_time(dir / "PKGBUILD", ec);
  if (ec) {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
    return;
  }
  const auto src_time = std::filesystem::last_write_time(srcinfo, ec);
  if (ec) {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
    return;
  }
  add_check(st, ".SRCINFO", pkg_time > src_time ? CheckKind::Warn : CheckKind::Ok,
            pkg_time > src_time ? "outdated" : "current");
}

void print_report(const StatusState& st) {
  if (st.pkgbase) {
    std::cout << "Package: " << *st.pkgbase << '\n';
    if (st.version) {
      std::cout << "Version: " << *st.version << '\n';
    }
    std::cout << '\n';
  }
  for (const auto& check : st.checks) {
    std::cout << format_check(check.label, check.kind, check.detail) << '\n';
  }
  if (!st.footer.empty()) {
    std::cout << '\n' << st.footer << '\n';
  }
}

int finish(const StatusState& st, bool check) {
  print_report(st);
  if (!check) {
    return 0;
  }
  for (const auto& c : st.checks) {
    if (c.kind == CheckKind::Fail) {
      return 1;
    }
  }
  return 0;
}

}  // namespace

int run_status(const Config& cfg, bool check) {
  require_tools(cfg.skip_ssh ? std::vector<std::string>{"git"}
                             : std::vector<std::string>{"git", "ssh"});

  StatusState st;
  const Workspace ws(cfg.cwd);
  const auto& dir = ws.dir();

  if (!file_exists(dir / "PKGBUILD")) {
    add_check(st, "PKGBUILD", CheckKind::Fail, "not found");
    add_ssh_and_push(st, check_ssh(cfg), false, false, false);
    return finish(st, check);
  }
  add_check(st, "PKGBUILD", CheckKind::Ok, "found");

  Srcinfo info;
  if (auto disk = try_parse_srcinfo_file(dir / ".SRCINFO")) {
    info = *disk;
    st.pkgbase = info.pkgbase;
    st.version = info.version_string();
  }

  const bool initialized = ws.is_initialized();
  add_check(st, "Workspace", initialized ? CheckKind::Ok : CheckKind::Fail,
            initialized ? "initialized" : "not initialized");

  if (!initialized) {
    add_check(st, "AUR repository", CheckKind::Fail, "not connected");
    add_ssh_and_push(st, check_ssh(cfg), false, false, false);
    st.footer = "Run `aurpush init` to initialize this workspace.";
    return finish(st, check);
  }

  const auto aur_remote = git::remote_url(dir, kAurRemote);
  // A missing or unreadable .SRCINFO must not make a correctly wired workspace
  // look disconnected: fall back to the pkgbase the remote itself names.
  std::string pkgbase = info.pkgbase;
  if (pkgbase.empty() && aur_remote) {
    pkgbase = pkgbase_from_remote_url(*aur_remote);
  }

  std::string url;
  bool connected = false;
  if (!pkgbase.empty()) {
    url = remote_url_for(cfg, pkgbase);
    connected = (aur_remote && (*aur_remote == url || is_aur_url(*aur_remote, pkgbase))) ||
                matching_aur_remote(ws, pkgbase, cfg).has_value();
  }

  Probe probe;
  if (connected) {
    probe = probe_aur(cfg, pkgbase, url, ws);
    if (!probe.remote_query_ok) {
      add_check(st, "AUR repository", CheckKind::Warn, "cannot check");
    } else if (probe.remote_master.empty()) {
      add_check(st, "AUR repository", CheckKind::Warn, "does not exist yet");
    } else {
      add_check(st, "AUR repository", CheckKind::Ok, "exists");
    }
    add_ssh_and_push(st, probe.ssh, probe.push_ok, probe.push_unknown, true);
  } else {
    add_check(st, "AUR repository", CheckKind::Fail, "not connected");
    add_ssh_and_push(st, check_ssh(cfg), false, false, false);
  }

  add_srcinfo_freshness(st, dir);
  add_check(st, "Changes", CheckKind::Plain, changes_detail(unpublished_changes(ws, info)));

  if (connected && probe.remote_query_ok) {
    add_relation(st, probe.relation);
  }

  return finish(st, check);
}

}  // namespace aurpush
