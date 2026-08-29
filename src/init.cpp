#include "aurpush/commands.hpp"

#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <filesystem>
#include <iostream>

namespace aurpush {
namespace {

void setup_git(const std::filesystem::path& dir, const std::string& url,
               const std::string& pkgbase, const Config& cfg) {
  if (!git::is_repo(dir)) {
    git::init_master(dir);
    git::remote_add(dir, kAurRemote, url);
    return;
  }

  git::ensure_master_branch(dir);

  const auto remotes = git::remotes(dir);
  if (!remotes.empty()) {
    const auto match = matching_aur_remote(dir, pkgbase, cfg);
    if (!match) {
      std::string found = remotes.front().second;
      throw Error(
          "this directory is already a Git repository for " + found +
          "; aurpush will not mix AUR history with another project");
    }
  }

  if (auto existing = git::remote_url(dir, kAurRemote)) {
    if (*existing != url && pkgbase_from_remote_url(*existing) != pkgbase) {
      throw Error("existing `aur` remote does not match pkgbase '" + pkgbase + "'");
    }
    if (*existing != url) {
      git::remote_set_url(dir, kAurRemote, url);
    }
  } else {
    git::remote_add(dir, kAurRemote, url);
  }
}

}  // namespace

int run_init(const Config& cfg) {
  const auto& dir = cfg.cwd;
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }

  const auto generated = generate_srcinfo(dir);
  const bool srcinfo_missing = !file_exists(dir / ".SRCINFO");
  const bool srcinfo_stale =
      !srcinfo_missing && normalize_text(read_file(dir / ".SRCINFO")) != generated.text;
  if (srcinfo_missing || srcinfo_stale) {
    write_file(dir / ".SRCINFO", generated.text);
  }

  const auto& info = generated.parsed;
  const std::string url = remote_url_for(cfg, info.pkgbase);

  if (git::is_repo(dir) && !git::remotes(dir).empty() &&
      !matching_aur_remote(dir, info.pkgbase, cfg)) {
    const auto remotes = git::remotes(dir);
    throw Error(
        "this directory is already a Git repository for " + remotes.front().second +
        "; aurpush will not mix AUR history with another project");
  }

  const auto auth = require_ssh(cfg);

  std::string remote_master;
  bool exists = false;
  try {
    remote_master = git::ls_remote_master(url);
    exists = !remote_master.empty();
  } catch (const Error&) {
    throw Error("failed to query the AUR repository at " + url);
  }

  setup_git(dir, url, info.pkgbase, cfg);

  git::fetch(dir, kAurRemote);

  const auto local_head = git::rev_parse(dir, "HEAD");
  if (!remote_master.empty() && !local_head) {
    preserve_pkgbuild_checkout(dir, kAurRemote + std::string("/master"));
  } else if (!remote_master.empty() && local_head && *local_head != remote_master) {
    const bool have_remote = git::has_object(dir, remote_master);
    const bool related =
        have_remote && (git::is_ancestor(dir, remote_master, *local_head) ||
                        git::is_ancestor(dir, *local_head, remote_master));
    if (!related) {
      throw Error(
          "local Git history is unrelated to the AUR repository; "
          "resolve this manually before continuing");
    }
  }

  write_marker(dir);
  ensure_gitignore(dir);

  std::cout << "Package: " << info.pkgbase << ' ' << info.version_string() << "\n\n";
  std::cout << "AUR SSH         authenticated as " << auth.username << '\n';
  if (exists) {
    std::cout << "AUR repository  exists\n";
  } else {
    std::cout << "AUR repository  does not exist yet\n";
  }
  std::cout << "Workspace       initialized\n\n";
  std::cout << "This workspace is ready. Run `aurpush -m \"message\"` to publish.\n";
  return 0;
}

}  // namespace aurpush
