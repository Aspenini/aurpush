#include "aurpush/commands.hpp"

#include "aurpush/colors.hpp"
#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/process.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <algorithm>
#include <iostream>
#include <map>

namespace aurpush {
namespace {

const char* kBehindRemote =
    "the AUR remote has commits not present locally; "
    "run `aurpush sync` before publishing";

void require_identity(const std::filesystem::path& dir) {
  if (!git::config(dir, "user.name")) {
    throw Error("git user.name is not set; configure it before publishing");
  }
  if (!git::config(dir, "user.email")) {
    throw Error("git user.email is not set; configure it before publishing");
  }
}

void print_names(const std::string& title, const std::vector<std::string>& names) {
  if (names.empty()) {
    return;
  }
  std::cout << title << ":\n";
  for (const auto& name : names) {
    std::cout << "  " << name << '\n';
  }
  std::cout << '\n';
}

std::string host_label(const std::string& url) {
  if (url.starts_with("ssh://")) {
    return "aur.archlinux.org";
  }
  return url;
}

void stage_files(Workspace& ws, const Srcinfo& info) {
  const auto& dir = ws.dir();
  git::add_force(dir, aur_file_set(ws, info));

  std::vector<std::string> missing;
  for (const auto& tracked : ws.tracked_files()) {
    std::error_code ec;
    if (!std::filesystem::exists(dir / tracked, ec)) {
      missing.push_back(tracked);
    }
  }
  git::rm(dir, missing);
  ws.refresh();
}

void ensure_not_behind(const std::filesystem::path& dir, const std::string& local,
                       const std::string& remote_master) {
  const auto rel = compare_heads(dir, local, remote_master);
  if (rel == Relation::Behind || rel == Relation::Diverged || rel == Relation::Unknown) {
    throw Error(kBehindRemote);
  }
}

int report_dry_run(const Workspace& ws, const Srcinfo& info, const std::string& message,
                   const std::string& url, bool srcinfo_would_change) {
  auto changes = unpublished_changes(ws, info);
  // .SRCINFO is regenerated as the first step of a real publish, so account for
  // it here even though nothing has been written to disk.
  if (srcinfo_would_change) {
    const auto& a = changes.added;
    const auto& m = changes.modified;
    if (std::find(a.begin(), a.end(), ".SRCINFO") == a.end() &&
        std::find(m.begin(), m.end(), ".SRCINFO") == m.end()) {
      changes.modified.push_back(".SRCINFO");
      std::sort(changes.modified.begin(), changes.modified.end());
    }
  }

  std::cout << "\nPackage: " << info.pkgbase << ' ' << info.version_string() << "\n\n";
  if (changes.empty()) {
    std::cout << "No file changes to publish.\n";
  } else {
    print_names("Added", changes.added);
    print_names("Modified", changes.modified);
    print_names("Deleted", changes.deleted);
    std::cout << "Commit:\n  " << message << "\n\n";
  }
  std::cout << "Would publish to " << host_label(url) << ".\n";
  std::cout << "Dry run: nothing was written, committed, or pushed.\n";
  return 0;
}

}  // namespace

int run_publish(const Config& cfg, const std::string& message, bool dry_run) {
  require_tools(cfg.skip_ssh ? std::vector<std::string>{"git", "makepkg"}
                             : std::vector<std::string>{"git", "ssh", "makepkg"});

  Workspace ws(cfg.cwd);
  const auto& dir = ws.dir();
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }
  if (!ws.is_initialized()) {
    throw Error("not an aurpush workspace; run `aurpush init` first");
  }

  std::cout << (dry_run ? "Checking .SRCINFO...\n" : "Updating .SRCINFO...\n");
  const auto generated = generate_srcinfo(dir);
  const auto& info = generated.parsed;
  const bool srcinfo_would_change =
      !file_exists(dir / ".SRCINFO") ||
      normalize_text(read_file(dir / ".SRCINFO")) != generated.text;
  if (!dry_run) {
    write_file(dir / ".SRCINFO", generated.text);
  }

  const auto aur_url = git::remote_url(dir, kAurRemote);
  if (!aur_url) {
    throw Error("workspace is missing the `aur` git remote");
  }
  const std::string url = remote_url_for(cfg, info.pkgbase);
  if (*aur_url != url && !is_aur_url(*aur_url, info.pkgbase)) {
    throw Error("pkgbase '" + info.pkgbase + "' does not match AUR remote '" +
                pkgbase_from_remote_url(*aur_url) + "'");
  }

  require_ssh(cfg);

  std::cout << (dry_run ? "Checking the AUR repository...\n"
                        : "Synchronizing AUR repository...\n");
  git::fetch(dir, kAurRemote);
  ws.refresh();

  const std::string remote_master =
      git::rev_parse(dir, kAurRemote + std::string("/master")).value_or("");

  require_push_access(cfg, info.pkgbase, !remote_master.empty());

  const auto local = git::rev_parse(dir, "HEAD");
  if (local && !remote_master.empty()) {
    ensure_not_behind(dir, *local, remote_master);
  }

  if (dry_run) {
    return report_dry_run(ws, info, message, url, srcinfo_would_change);
  }

  stage_files(ws, info);
  const bool new_changes = git::index_has_changes(dir);

  std::cout << "\nPackage: " << info.pkgbase << ' ' << info.version_string() << "\n\n";

  if (new_changes) {
    std::map<char, std::vector<std::string>> grouped;
    for (const auto& [status, name] : git::cached_changes(dir)) {
      grouped[status].push_back(name);
    }
    print_names("Added", grouped['A']);
    print_names("Modified", grouped['M']);
    print_names("Deleted", grouped['D']);

    require_identity(dir);
    std::cout << "Commit:\n  " << message << "\n\n";
    git::commit(dir, message);
    ws.refresh();
  } else {
    const auto head = git::rev_parse(dir, "HEAD");
    if (!head) {
      std::cout << "Nothing to publish.\n";
      return 0;
    }
    // With no remote yet, an existing local commit is a retry of a first publish
    // whose push did not land.
    if (!remote_master.empty()) {
      const auto rel = compare_heads(dir, *head, remote_master);
      if (rel == Relation::Equal) {
        std::cout << "Nothing to publish.\n";
        return 0;
      }
      if (rel != Relation::Ahead) {
        throw Error(kBehindRemote);
      }
    }
    std::cout << "No new file changes.\n\n";
  }

  std::cout << "Publishing to " << host_label(url) << "...\n";
  git::push(dir, kAurRemote, "HEAD:master");
  const Glyphs& g = glyphs();
  std::cout << g.green << g.ok << g.reset << " Published successfully\n";
  return 0;
}

}  // namespace aurpush
