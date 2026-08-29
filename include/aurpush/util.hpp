#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aurpush {

std::string read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, std::string_view data);
bool file_exists(const std::filesystem::path& path);

std::string trim(std::string_view s);
std::string rtrim(std::string_view s);
std::vector<std::string> split_lines(std::string_view s);
std::string normalize_text(std::string_view s);

bool looks_like_utf8_locale();
bool stdout_is_tty();

}  // namespace aurpush
