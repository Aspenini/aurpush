#include "aurpush/commands.hpp"

#include "aurpush/colors.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

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

void add_ssh_and_push(StatusState& st, const SshAuth& ssh, bool push_ok, bool push_unknown,
                      bool report_push) {
  if (ssh.ok) {
    add_check(st, "AUR SSH", CheckKind::Ok, "authenticated");
  } else {
    add_check(st, "AUR SSH", CheckKind::Fail, "not authenticated");
    return;
  }
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
      break;
    case Relation::Behind:
      add_check(st, "Remote", CheckKind::Fail, "behind remote");
      break;
    case Relation::Diverged:
      add_check(st, "Remote", CheckKind::Fail, "diverged");
      break;
    case Relation::Unknown:
      add_check(st, "Remote", CheckKind::Warn, "differs from remote");
      break;
  }
}

std::string changes_detail(const TreeDiff& changes) {
  std::string detail;
  if (changes.count() == 0) {
    return "none";
  }
  if (changes.count() == 1) {
    detail = "1 unpublished file";
  } else {
    detail = std::to_string(changes.count()) + " unpublished files";
  }
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

void add_srcinfo_freshness(StatusState& st, const std::filesystem::path& dir) {
  const auto srcinfo = dir / ".SRCINFO";
  const auto pkgbuild = dir / "PKGBUILD";
  if (!file_exists(srcinfo)) {
    add_check(st, ".SRCINFO", CheckKind::Fail, "missing");
    return;
  }
  std::error_code ec;
  const auto pkg_time = std::filesystem::last_write_time(pkgbuild, ec);
  if (ec) {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
    return;
  }
  const auto src_time = std::filesystem::last_write_time(srcinfo, ec);
  if (ec) {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
    return;
  }
  if (pkg_time > src_time) {
    add_check(st, ".SRCINFO", CheckKind::Warn, "outdated");
  } else {
    add_check(st, ".SRCINFO", CheckKind::Ok, "current");
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

int finish(const StatusState& st, bool check) {
  print_report(st);
  if (!check) {
    return 0;
  }
  for (const auto& [label, body] : st.checks) {
    if (body.first == CheckKind::Fail) {
      return 1;
    }
  }
  return 0;
}

}  // namespace

int run_status(const Config& cfg, bool check) {
  StatusState st;
  const auto& dir = cfg.cwd;
  const bool pkgbuild = file_exists(dir / "PKGBUILD");

  if (!pkgbuild) {
    add_check(st, "PKGBUILD", CheckKind::Fail, "not found");
    add_ssh_and_push(st, check_ssh(cfg), false, false, false);
    return finish(st, check);
  }

  add_check(st, "PKGBUILD", CheckKind::Ok, "found");

  Srcinfo info;
  if (auto disk = try_parse_srcinfo_file(dir / ".SRCINFO")) {
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
    add_ssh_and_push(st, check_ssh(cfg), false, false, false);
    st.footer = "Run `aurpush init` to initialize this workspace.";
    return finish(st, check);
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

  Probe probe;
  if (connected && info.valid()) {
    probe = probe_aur(cfg, info.pkgbase, url, dir);
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
  add_check(st, "Changes", CheckKind::Plain, changes_detail(unpublished_changes(dir, info)));

  if (connected && probe.remote_query_ok) {
    add_relation(st, probe.relation);
  }

  return finish(st, check);
}

}  // namespace aurpush
