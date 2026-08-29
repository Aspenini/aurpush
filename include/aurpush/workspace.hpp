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

bool has_marker(const std::filesystem::path& dir);
void write_marker(const std::filesystem::path& dir);

void ensure_gitignore(const std::filesystem::path& dir);

bool is_aur_url(std::string_view url, std::string_view pkgbase);
std::optional<std::string> matching_aur_remote(const std::filesystem::path& dir,
                                               std::string_view pkgbase,
                                               const Config& cfg);

std::vector<std::string> aur_file_set(const std::filesystem::path& dir,
                                      const Srcinfo& info);

struct TreeDiff {
  std::vector<std::string> added;
  std::vector<std::string> modified;
  std::vector<std::string> deleted;
  int count() const {
    return static_cast<int>(added.size() + modified.size() + deleted.size());
  }
  bool empty() const { return count() == 0; }
};

TreeDiff unpublished_changes(const std::filesystem::path& dir, const Srcinfo& info);

}  // namespace aurpush
