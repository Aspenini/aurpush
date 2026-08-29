#include "aurpush/commands.hpp"

#include "aurpush/colors.hpp"
#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/probe.hpp"
#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <iostream>
#include <map>

namespace aurpush {
namespace {

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
  if (url.rfind("ssh://", 0) == 0) {
    return "aur.archlinux.org";
  }
  return url;
}

void stage_files(const std::filesystem::path& dir, const Srcinfo& info) {
  const auto wanted = aur_file_set(dir, info);
  git::add_force(dir, wanted);

  std::vector<std::string> missing;
  for (const auto& tracked : git::tracked_files(dir)) {
    std::error_code ec;
    if (!std::filesystem::exists(dir / tracked, ec)) {
      missing.push_back(tracked);
    }
  }
  git::rm(dir, missing);
}

}  // namespace

int run_publish(const Config& cfg, const std::string& message) {
  const auto& dir = cfg.cwd;
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }
  if (!has_marker(dir) || !git::is_repo(dir)) {
    throw Error("not an aurpush workspace; run `aurpush init` first");
  }

  std::cout << "Updating .SRCINFO...\n";
  const auto generated = generate_srcinfo(dir);
  write_file(dir / ".SRCINFO", generated.text);
  const auto& info = generated.parsed;

  const auto aur_url = git::remote_url(dir, kAurRemote);
  if (!aur_url) {
    throw Error("workspace is missing the `aur` git remote");
  }
  const std::string expected = remote_url_for(cfg, info.pkgbase);
  if (*aur_url != expected && !is_aur_url(*aur_url, info.pkgbase)) {
    throw Error("pkgbase '" + info.pkgbase + "' does not match AUR remote '" +
                pkgbase_from_remote_url(*aur_url) + "'");
  }

  require_ssh(cfg);

  std::cout << "Synchronizing AUR repository...\n";
  git::fetch(dir, kAurRemote);

  const std::string url = expected;
  const std::string remote_master =
      git::rev_parse(dir, kAurRemote + std::string("/master")).value_or("");

  require_push_access(cfg, info.pkgbase, !remote_master.empty());

  const auto local = git::rev_parse(dir, "HEAD");
  if (local && !remote_master.empty()) {
    const auto rel = compare_heads(dir, *local, remote_master);
    if (rel == Relation::Behind || rel == Relation::Diverged || rel == Relation::Unknown) {
      throw Error(
          "the AUR remote has commits not present locally; "
          "synchronize before publishing");
    }
  }

  stage_files(dir, info);
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
  } else {
    const auto head = git::rev_parse(dir, "HEAD");
    if (!head) {
      std::cout << "Nothing to publish.\n";
      return 0;
    }
    if (remote_master.empty()) {
      // first publish retry of an unpushed commit
    } else {
      const auto rel = compare_heads(dir, *head, remote_master);
      if (rel == Relation::Equal) {
        std::cout << "Nothing to publish.\n";
        return 0;
      }
      if (rel != Relation::Ahead) {
        throw Error(
            "the AUR remote has commits not present locally; "
            "synchronize before publishing");
      }
    }
    std::cout << "No new file changes.\n\n";
  }

  std::cout << "Publishing to " << host_label(url) << "...\n";
  git::push(dir, kAurRemote, "HEAD:master");
  const Glyphs g = glyphs();
  std::cout << g.green << g.ok << g.reset << " Published successfully\n";
  return 0;
}

}  // namespace aurpush
