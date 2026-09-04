#include "aurpush/commands.hpp"

#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/process.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <iostream>

namespace aurpush {

int run_sync(const Config& cfg) {
  require_tools(cfg.skip_ssh ? std::vector<std::string>{"git"}
                             : std::vector<std::string>{"git", "ssh"});

  const Workspace ws(cfg.cwd);
  const auto& dir = ws.dir();
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }
  if (!ws.is_initialized()) {
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

  switch (compare_heads(dir, local, remote.value_or(""))) {
    case Relation::NoRemote:
      std::cout << "AUR repository does not exist yet.\n";
      return 0;
    case Relation::Equal:
      std::cout << (remote ? "Already up to date.\n" : "AUR repository does not exist yet.\n");
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
      throw Error("local history has diverged from the AUR remote; resolve this manually");
    case Relation::Unknown:
      // Distinct from divergence: the remote commit is not in this repository at
      // all, so git cannot say how the two histories relate.
      throw Error("the AUR commit " + remote.value_or("(unknown)").substr(0, 12) +
                  " is not present locally; run `git fetch " + kAurRemote +
                  "` and resolve this manually");
  }
  return 0;
}

}  // namespace aurpush
