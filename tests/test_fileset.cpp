#include "test.hpp"

#include "aurpush/srcinfo.hpp"
#include "aurpush/util.hpp"
#include "aurpush/workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir() {
  static int n = 0;
  auto dir = fs::temp_directory_path() /
             ("aurpush-fileset-" + std::to_string(getpid()) + "-" +
              std::to_string(++n));
  fs::create_directories(dir);
  return dir;
}

bool contains(const std::vector<std::string>& files, const std::string& name) {
  return std::find(files.begin(), files.end(), name) != files.end();
}

}  // namespace

TEST(fileset_includes_local_sources_and_skips_urls) {
  const auto dir = make_temp_dir();
  aurpush::write_file(dir / "PKGBUILD", "pkgname=foo\n");
  aurpush::write_file(dir / ".SRCINFO", "pkgbase = foo\n");
  aurpush::write_file(dir / "example.patch", "diff\n");
  aurpush::write_file(dir / "LICENSE", "MIT\n");
  fs::create_directories(dir / "src");
  aurpush::write_file(dir / "src" / "junk.c", "int x;\n");

  auto info = aurpush::parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source = https://example.com/foo.tar.gz\n"
      "	source = example.patch\n"
      "	install = foo.install\n"
      "pkgname = foo\n");
  aurpush::write_file(dir / "foo.install", "post_install() { :; }\n");

  const auto files = aurpush::aur_file_set(dir, info);
  REQUIRE(contains(files, "PKGBUILD"));
  REQUIRE(contains(files, ".SRCINFO"));
  REQUIRE(contains(files, "example.patch"));
  REQUIRE(contains(files, "foo.install"));
  REQUIRE(contains(files, "LICENSE"));
  REQUIRE(!contains(files, "src/junk.c"));
  fs::remove_all(dir);
}
