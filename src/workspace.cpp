#include "aurpush/workspace.hpp"

#include "aurpush/git.hpp"
#include "aurpush/util.hpp"

#include <algorithm>
#include <set>

namespace aurpush {
namespace {

const std::vector<std::string> kGitignoreLines = {
    ".aurpush",
    "src/",
    "pkg/",
    "*.pkg.tar*",
    "*.src.tar*",
};

bool ignored_name(const std::string& rel) {
  if (rel == ".aurpush" || rel == ".git" || rel.rfind(".git/", 0) == 0) {
    return true;
  }
  if (rel == "src" || rel.rfind("src/", 0) == 0) {
    return true;
  }
  if (rel == "pkg" || rel.rfind("pkg/", 0) == 0) {
    return true;
  }
  if (rel.find(".pkg.tar") != std::string::npos ||
      rel.find(".src.tar") != std::string::npos) {
    return true;
  }
  if (!rel.empty() && rel.back() == '~') {
    return true;
  }
  if (rel.size() >= 4 && rel.compare(rel.size() - 4, 4, ".swp") == 0) {
    return true;
  }
  return false;
}

void add_if_exists(std::set<std::string>& files, const std::filesystem::path& dir,
                   const std::string& rel) {
  if (rel.empty() || ignored_name(rel)) {
    return;
  }
  const auto path = dir / rel;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    files.insert(rel);
  }
}

void add_tree(std::set<std::string>& files, const std::filesystem::path& dir,
              const std::string& rel) {
  const auto path = dir / rel;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return;
  }
  if (std::filesystem::is_regular_file(path, ec)) {
    add_if_exists(files, dir, rel);
    return;
  }
  if (std::filesystem::is_directory(path, ec)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      const auto rel_path = std::filesystem::relative(entry.path(), dir).generic_string();
      add_if_exists(files, dir, rel_path);
    }
  }
}

std::string file_contents(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    return {};
  }
  return read_file(path);
}

}  // namespace

bool has_marker(const std::filesystem::path& dir) {
  return file_exists(dir / kMarkerName);
}

void write_marker(const std::filesystem::path& dir) {
  write_file(dir / kMarkerName, "aurpush workspace\n");
}

void ensure_gitignore(const std::filesystem::path& dir) {
  const auto path = dir / ".gitignore";
  std::string existing;
  std::set<std::string> have;
  if (file_exists(path)) {
    existing = read_file(path);
    for (const auto& line : split_lines(existing)) {
      have.insert(trim(line));
    }
  }
  std::string extra;
  for (const auto& line : kGitignoreLines) {
    if (!have.count(line)) {
      extra += line;
      extra += '\n';
    }
  }
  if (extra.empty()) {
    return;
  }
  if (!existing.empty() && existing.back() != '\n') {
    existing += '\n';
  }
  write_file(path, existing + extra);
}

bool is_aur_url(std::string_view url, std::string_view pkgbase) {
  if (pkgbase_from_remote_url(url) != pkgbase) {
    return false;
  }
  return url.find("aur.archlinux.org/") != std::string_view::npos ||
         url.find("aur@aur.archlinux.org:") != std::string_view::npos;
}

std::optional<std::string> matching_aur_remote(const std::filesystem::path& dir,
                                               std::string_view pkgbase,
                                               const Config& cfg) {
  if (!git::is_repo(dir)) {
    return std::nullopt;
  }
  const std::string expected = remote_url_for(cfg, pkgbase);
  for (const auto& [name, url] : git::remotes(dir)) {
    if (url == expected) {
      return name;
    }
    if (cfg.remote_url.empty() && is_aur_url(url, pkgbase)) {
      return name;
    }
  }
  return std::nullopt;
}

std::vector<std::string> aur_file_set(const std::filesystem::path& dir,
                                      const Srcinfo& info) {
  std::set<std::string> files;
  add_if_exists(files, dir, "PKGBUILD");
  add_if_exists(files, dir, ".SRCINFO");
  add_if_exists(files, dir, ".gitignore");
  add_if_exists(files, dir, "LICENSE");
  add_if_exists(files, dir, "REUSE.toml");
  add_tree(files, dir, "LICENSES");

  for (const auto& install : info.install_files) {
    add_if_exists(files, dir, install);
  }
  for (const auto& changelog : info.changelogs) {
    add_if_exists(files, dir, changelog);
  }
  for (const auto& source : info.sources) {
    add_if_exists(files, dir, source_local_path(source));
  }

  if (git::is_repo(dir)) {
    for (const auto& tracked : git::tracked_files(dir)) {
      if (ignored_name(tracked)) {
        continue;
      }
      add_if_exists(files, dir, tracked);
    }
  }

  return {files.begin(), files.end()};
}

TreeDiff unpublished_changes(const std::filesystem::path& dir, const Srcinfo& info) {
  TreeDiff diff;
  const auto wanted = aur_file_set(dir, info);
  std::set<std::string> wanted_set(wanted.begin(), wanted.end());
  std::set<std::string> tracked;
  if (git::is_repo(dir)) {
    for (const auto& f : git::tracked_files(dir)) {
      tracked.insert(f);
    }
  }

  for (const auto& file : wanted) {
    const auto on_disk = file_contents(dir / file);
    if (!tracked.count(file)) {
      diff.added.push_back(file);
      continue;
    }
    const auto from_git = git::show_file(dir, "HEAD", file);
    if (!from_git || *from_git != on_disk) {
      diff.modified.push_back(file);
    }
  }

  for (const auto& file : tracked) {
    if (ignored_name(file)) {
      continue;
    }
    std::error_code ec;
    if (!std::filesystem::exists(dir / file, ec)) {
      diff.deleted.push_back(file);
    }
  }

  return diff;
}

}  // namespace aurpush
