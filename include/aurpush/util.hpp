#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aurpush {

std::string read_file(const std::filesystem::path& path);

// Writes through a sibling temporary file and renames, so readers never observe
// a half-written file and a failed write leaves the original intact.
void write_file(const std::filesystem::path& path, std::string_view data);

bool file_exists(const std::filesystem::path& path);

// True for a path that stays inside the package directory: relative, with no
// ".." component. PKGBUILD sources that escape the build directory are rejected
// rather than staged.
bool is_contained_relative_path(std::string_view rel);

std::string trim(std::string_view s);
std::string rtrim(std::string_view s);
std::vector<std::string> split_lines(std::string_view s);
std::string normalize_text(std::string_view s);
std::string join(const std::vector<std::string>& parts, std::string_view sep);

// Returns the variable's value only when it is set and non-empty.
std::optional<std::string> env_var(const char* name);

bool looks_like_utf8_locale();
bool stdout_is_tty();

}  // namespace aurpush
