#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aurpush/config.hpp"

namespace aurpush {

struct Srcinfo {
  std::string pkgbase;
  std::string pkgver;
  std::string pkgrel;
  std::string epoch;
  std::vector<std::string> pkgnames;
  std::vector<std::string> sources;
  std::vector<std::string> install_files;
  std::vector<std::string> changelogs;

  std::string version_string() const;
  bool valid() const { return !pkgbase.empty() && !pkgver.empty() && !pkgrel.empty(); }
};

Srcinfo parse_srcinfo(std::string_view text);

bool is_remote_source(std::string_view source);
std::string source_local_path(std::string_view source);

struct GeneratedSrcinfo {
  std::string text;
  Srcinfo parsed;
};

GeneratedSrcinfo generate_srcinfo(const std::filesystem::path& dir);
std::optional<Srcinfo> try_parse_srcinfo_file(const std::filesystem::path& path);

}  // namespace aurpush
