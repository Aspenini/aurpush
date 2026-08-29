#include "aurpush/srcinfo.hpp"

#include "aurpush/error.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

namespace aurpush {
namespace {

std::string unquote_source_filename(std::string s) {
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

}  // namespace

std::string Srcinfo::version_string() const {
  if (!epoch.empty()) {
    return epoch + ":" + pkgver + "-" + pkgrel;
  }
  return pkgver + "-" + pkgrel;
}

Srcinfo parse_srcinfo(std::string_view text) {
  Srcinfo info;
  for (const auto& raw : split_lines(text)) {
    std::string line = trim(raw);
    if (line.empty()) {
      continue;
    }
    const auto eq = line.find(" = ");
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    const std::string value = line.substr(eq + 3);
    if (key == "pkgbase" && info.pkgbase.empty()) {
      info.pkgbase = value;
    } else if (key == "pkgver" && info.pkgver.empty()) {
      info.pkgver = value;
    } else if (key == "pkgrel" && info.pkgrel.empty()) {
      info.pkgrel = value;
    } else if (key == "epoch" && info.epoch.empty()) {
      info.epoch = value;
    } else if (key == "pkgname") {
      info.pkgnames.push_back(value);
    } else if (key == "source" || key.rfind("source_", 0) == 0) {
      info.sources.push_back(value);
    } else if (key == "install") {
      info.install_files.push_back(value);
    } else if (key == "changelog") {
      info.changelogs.push_back(value);
    }
  }
  if (info.pkgbase.empty() && !info.pkgnames.empty()) {
    info.pkgbase = info.pkgnames.front();
  }
  return info;
}

bool is_remote_source(std::string_view source) {
  std::string payload(source);
  const auto sep = payload.find("::");
  if (sep != std::string::npos) {
    payload = payload.substr(sep + 2);
  }
  return payload.find("://") != std::string::npos;
}

std::string source_local_path(std::string_view source) {
  if (is_remote_source(source)) {
    return {};
  }
  std::string s(source);
  const auto sep = s.find("::");
  if (sep != std::string::npos) {
    s = s.substr(0, sep);
  }
  s = unquote_source_filename(s);
  return s;
}

GeneratedSrcinfo generate_srcinfo(const std::filesystem::path& dir) {
  const auto result = run({"makepkg", "--printsrcinfo"}, dir);
  if (!result.ok()) {
    std::string msg = "failed to generate .SRCINFO with makepkg --printsrcinfo";
    const std::string err = trim(result.err.empty() ? result.out : result.err);
    if (!err.empty()) {
      msg += "\n" + err;
    }
    throw Error(msg);
  }
  GeneratedSrcinfo gen;
  gen.text = normalize_text(result.out);
  gen.parsed = parse_srcinfo(gen.text);
  if (!gen.parsed.valid()) {
    throw Error("makepkg --printsrcinfo did not produce pkgbase, pkgver, and pkgrel");
  }
  return gen;
}

std::optional<Srcinfo> try_parse_srcinfo_file(const std::filesystem::path& path) {
  if (!file_exists(path)) {
    return std::nullopt;
  }
  try {
    Srcinfo info = parse_srcinfo(read_file(path));
    if (!info.valid()) {
      return std::nullopt;
    }
    return info;
  } catch (...) {
    return std::nullopt;
  }
}

}  // namespace aurpush
