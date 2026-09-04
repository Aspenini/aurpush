#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurpush/config.hpp"
#include "aurpush/srcinfo.hpp"

namespace aurpush {

inline constexpr const char* kMarkerName = ".aurpush";
inline constexpr const char* kAurRemote = "aur";

// One package directory, with the answers to repeated git questions cached for
// the lifetime of a command. Answering "is this a repo?" costs two forked git
// processes, and it used to be asked nine times per run.
class Workspace {
 public:
  explicit Workspace(std::filesystem::path dir);

  const std::filesystem::path& dir() const { return dir_; }
  bool is_repo() const { return is_repo_; }
  bool has_marker() const;
  bool is_initialized() const { return is_repo_ && has_marker(); }

  // Paths tracked at HEAD. Empty when this is not a repo or has no commits.
  const std::vector<std::string>& tracked_files() const;

  // Drops cached git state after an operation that changes the repository.
  void refresh();

 private:
  std::filesystem::path dir_;
  bool is_repo_ = false;
  mutable std::optional<std::vector<std::string>> tracked_;
};

void write_marker(const std::filesystem::path& dir);

void ensure_gitignore(const std::filesystem::path& dir);
void preserve_pkgbuild_checkout(const std::filesystem::path& dir, const std::string& ref);

bool is_aur_url(std::string_view url, std::string_view pkgbase);
std::optional<std::string> matching_aur_remote(const Workspace& ws, std::string_view pkgbase,
                                               const Config& cfg);

std::vector<std::string> aur_file_set(const Workspace& ws, const Srcinfo& info);

struct TreeDiff {
  std::vector<std::string> added;
  std::vector<std::string> modified;
  std::vector<std::string> deleted;
  int count() const {
    return static_cast<int>(added.size() + modified.size() + deleted.size());
  }
  bool empty() const { return count() == 0; }
};

// What differs between the working tree and the last published commit.
TreeDiff unpublished_changes(const Workspace& ws, const Srcinfo& info);

}  // namespace aurpush
