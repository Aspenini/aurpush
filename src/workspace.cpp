#include "aurpush/workspace.hpp"

#include "aurpush/git.hpp"
#include "aurpush/util.hpp"

#include <set>
#include <utility>

namespace aurpush {
namespace {

const std::vector<std::string> kGitignoreLines = {
    ".aurpush",
    "src/",
    "pkg/",
    "*.pkg.tar*",
    "*.src.tar*",
};

std::string_view basename_of(std::string_view rel) {
  const auto slash = rel.find_last_of('/');
  return slash == std::string_view::npos ? rel : rel.substr(slash + 1);
}

bool under_directory(const std::string& rel, std::string_view name) {
  return rel == name || rel.starts_with(std::string(name) + "/");
}

// Build products and editor droppings never belong in the AUR repository.
bool ignored_name(const std::string& rel) {
  if (under_directory(rel, ".aurpush") || under_directory(rel, ".git") ||
      under_directory(rel, "src") || under_directory(rel, "pkg")) {
    return true;
  }
  // Scoped to the file name so a directory that happens to contain ".pkg.tar"
  // does not silently exclude everything beneath it.
  const std::string_view base = basename_of(rel);
  if (base.find(".pkg.tar") != std::string_view::npos ||
      base.find(".src.tar") != std::string_view::npos) {
    return true;
  }
  return base.ends_with("~") || base.ends_with(".swp");
}

void add_if_exists(std::set<std::string>& files, const std::filesystem::path& dir,
                   const std::string& rel) {
  // Rejects absolute paths and anything reaching outside the package directory;
  // makepkg would not accept such a source either.
  if (!is_contained_relative_path(rel) || ignored_name(rel)) {
    return;
  }
  std::error_code ec;
  if (std::filesystem::exists(dir / rel, ec)) {
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
  if (!std::filesystem::is_directory(path, ec)) {
    return;
  }
  std::error_code iter_ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(path, iter_ec)) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) || entry_ec) {
      continue;
    }
    const auto rel_path =
        std::filesystem::relative(entry.path(), dir, entry_ec).generic_string();
    if (entry_ec) {
      continue;
    }
    add_if_exists(files, dir, rel_path);
  }
}

}  // namespace

Workspace::Workspace(std::filesystem::path dir) : dir_(std::move(dir)) {
  is_repo_ = git::is_repo(dir_);
}

bool Workspace::has_marker() const { return file_exists(dir_ / kMarkerName); }

const std::vector<std::string>& Workspace::tracked_files() const {
  if (!tracked_) {
    tracked_ = is_repo_ ? git::tracked_files(dir_) : std::vector<std::string>{};
  }
  return *tracked_;
}

void Workspace::refresh() {
  is_repo_ = git::is_repo(dir_);
  tracked_.reset();
}

void write_marker(const std::filesystem::path& dir) {
  write_file(dir / kMarkerName, "aurpush workspace\n");
}

void preserve_pkgbuild_checkout(const std::filesystem::path& dir, const std::string& ref) {
  const auto pkgbuild = dir / "PKGBUILD";
  const auto srcinfo = dir / ".SRCINFO";
  std::optional<std::string> pkg_content;
  std::optional<std::string> src_content;
  if (file_exists(pkgbuild)) {
    pkg_content = read_file(pkgbuild);
    std::filesystem::remove(pkgbuild);
  }
  if (file_exists(srcinfo)) {
    src_content = read_file(srcinfo);
    std::filesystem::remove(srcinfo);
  }
  git::checkout_ref(dir, ref);
  if (pkg_content) {
    write_file(pkgbuild, *pkg_content);
  }
  if (src_content) {
    write_file(srcinfo, *src_content);
  }
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

std::optional<std::string> matching_aur_remote(const Workspace& ws, std::string_view pkgbase,
                                               const Config& cfg) {
  if (!ws.is_repo()) {
    return std::nullopt;
  }
  const std::string expected = remote_url_for(cfg, pkgbase);
  for (const auto& [name, url] : git::remotes(ws.dir())) {
    if (url == expected) {
      return name;
    }
    if (cfg.remote_url.empty() && is_aur_url(url, pkgbase)) {
      return name;
    }
  }
  return std::nullopt;
}

std::vector<std::string> aur_file_set(const Workspace& ws, const Srcinfo& info) {
  const auto& dir = ws.dir();
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

  // Anything already published stays published unless it is gone from disk.
  for (const auto& tracked : ws.tracked_files()) {
    add_if_exists(files, dir, tracked);
  }

  return {files.begin(), files.end()};
}

TreeDiff unpublished_changes(const Workspace& ws, const Srcinfo& info) {
  TreeDiff diff;
  const auto& dir = ws.dir();
  const auto wanted = aur_file_set(ws, info);
  const auto& tracked_list = ws.tracked_files();
  const std::set<std::string> tracked(tracked_list.begin(), tracked_list.end());

  // One git call for every modification, rather than reading each file and
  // shelling out to `git show` per path.
  std::set<std::string> changed;
  if (ws.is_repo()) {
    for (auto& name : git::worktree_diff_names(dir, "HEAD")) {
      changed.insert(std::move(name));
    }
  }

  for (const auto& file : wanted) {
    if (!tracked.count(file)) {
      diff.added.push_back(file);
    } else if (changed.count(file)) {
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
