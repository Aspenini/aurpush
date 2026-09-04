#include "aurpush/util.hpp"

#include "aurpush/error.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace aurpush {
namespace {

std::string upper(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw Error("failed to read " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_file(const std::filesystem::path& path, std::string_view data) {
  const auto tmp =
      path.string() + ".aurpush-tmp." + std::to_string(static_cast<long>(getpid()));
  bool wrote = false;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (out) {
      out << data;
      out.flush();
      wrote = static_cast<bool>(out);
    }
  }
  if (!wrote) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw Error("failed to write " + path.string());
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    throw Error("failed to write " + path.string() + ": " + ec.message());
  }
}

bool file_exists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

bool is_contained_relative_path(std::string_view rel) {
  if (rel.empty()) {
    return false;
  }
  const std::filesystem::path path{rel};
  if (path.is_absolute()) {
    return false;
  }
  for (const auto& part : path) {
    if (part == "..") {
      return false;
    }
  }
  return true;
}

std::string trim(std::string_view s) {
  std::size_t begin = 0;
  while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
    ++begin;
  }
  std::size_t end = s.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return std::string(s.substr(begin, end - begin));
}

std::string rtrim(std::string_view s) {
  std::size_t end = s.size();
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
    --end;
  }
  return std::string(s.substr(0, end));
}

std::vector<std::string> split_lines(std::string_view s) {
  std::vector<std::string> lines;
  std::string current;
  for (char c : s) {
    if (c == '\n') {
      if (!current.empty() && current.back() == '\r') {
        current.pop_back();
      }
      lines.push_back(std::move(current));
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    if (current.back() == '\r') {
      current.pop_back();
    }
    lines.push_back(std::move(current));
  }
  return lines;
}

std::string normalize_text(std::string_view s) {
  std::string out;
  for (const auto& line : split_lines(s)) {
    out += rtrim(line);
    out += '\n';
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out += sep;
    }
    out += parts[i];
  }
  return out;
}

std::optional<std::string> env_var(const char* name) {
  const char* value = std::getenv(name);
  if (!value || !*value) {
    return std::nullopt;
  }
  return std::string(value);
}

bool looks_like_utf8_locale() {
  for (const char* key : {"LC_ALL", "LC_CTYPE", "LANG"}) {
    const auto value = env_var(key);
    if (!value) {
      continue;
    }
    const std::string s = upper(*value);
    if (s.find("UTF-8") != std::string::npos || s.find("UTF8") != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool stdout_is_tty() { return isatty(STDOUT_FILENO) == 1; }

}  // namespace aurpush
