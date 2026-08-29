#include "aurpush/commands.hpp"

#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <iostream>

namespace aurpush {
namespace {

const char* kDiverged =
    "local history has diverged from the AUR remote; resolve this manually";

}  // namespace

int run_sync(const Config& cfg) {
  const auto& dir = cfg.cwd;
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }
  if (!has_marker(dir) || !git::is_repo(dir)) {
    throw Error("not an aurpush workspace; run `aurpush init` first");
  }

  const auto aur_url = git::remote_url(dir, kAurRemote);
  if (!aur_url) {
    throw Error("workspace is missing the `aur` git remote");
  }

  std::string pkgbase;
  if (auto disk = try_parse_srcinfo_file(dir / ".SRCINFO")) {
    pkgbase = disk->pkgbase;
  } else {
    pkgbase = pkgbase_from_remote_url(*aur_url);
  }
  if (pkgbase.empty()) {
    throw Error("could not determine pkgbase");
  }
  const std::string expected = remote_url_for(cfg, pkgbase);
  if (*aur_url != expected && !is_aur_url(*aur_url, pkgbase)) {
    throw Error("pkgbase '" + pkgbase + "' does not match AUR remote '" +
                pkgbase_from_remote_url(*aur_url) + "'");
  }

  require_ssh(cfg);

  std::cout << "Fetching AUR repository...\n";
  git::fetch(dir, kAurRemote);

  const std::string remote_ref = kAurRemote + std::string("/master");
  const auto remote = git::rev_parse(dir, remote_ref);
  const auto local = git::rev_parse(dir, "HEAD");
  const auto rel = compare_heads(dir, local, remote.value_or(""));

  switch (rel) {
    case Relation::NoRemote:
    case Relation::Equal:
      if (!remote) {
        std::cout << "AUR repository does not exist yet.\n";
      } else {
        std::cout << "Already up to date.\n";
      }
      return 0;
    case Relation::Ahead:
      std::cout << "Local is ahead of AUR; nothing to sync.\n";
      return 0;
    case Relation::NoLocal:
      preserve_pkgbuild_checkout(dir, remote_ref);
      std::cout << "Checked out AUR master.\n";
      return 0;
    case Relation::Behind:
      git::merge_ff_only(dir, remote_ref);
      std::cout << "Fast-forwarded to " << remote_ref << ".\n";
      return 0;
    case Relation::Diverged:
    case Relation::Unknown:
      throw Error(kDiverged);
  }
  return 0;
}

}  // namespace aurpush
