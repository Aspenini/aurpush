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

  const auto files = aurpush::aur_file_set(aurpush::Workspace(dir), info);
  REQUIRE(contains(files, "PKGBUILD"));
  REQUIRE(contains(files, ".SRCINFO"));
  REQUIRE(contains(files, "example.patch"));
  REQUIRE(contains(files, "foo.install"));
  REQUIRE(contains(files, "LICENSE"));
  REQUIRE(!contains(files, "src/junk.c"));
  fs::remove_all(dir);
}

TEST(fileset_includes_arch_specific_local_sources) {
  const auto dir = make_temp_dir();
  aurpush::write_file(dir / "PKGBUILD", "pkgname=foo\n");
  aurpush::write_file(dir / ".SRCINFO", "pkgbase = foo\n");
  aurpush::write_file(dir / "extra.patch", "diff\n");

  auto info = aurpush::parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source_x86_64 = extra.patch\n"
      "	source_x86_64 = https://example.com/foo.tar.gz\n"
      "pkgname = foo\n");

  const auto files = aurpush::aur_file_set(aurpush::Workspace(dir), info);
  REQUIRE(contains(files, "PKGBUILD"));
  REQUIRE(contains(files, "extra.patch"));
  REQUIRE(!contains(files, "foo.tar.gz"));
  fs::remove_all(dir);
}

// A PKGBUILD source is not allowed to reach outside the package directory;
// staging such a path would either fail in git or capture an unrelated file.
TEST(fileset_rejects_sources_outside_the_package_directory) {
  const auto dir = make_temp_dir();
  aurpush::write_file(dir / "PKGBUILD", "pkgname=foo\n");
  aurpush::write_file(dir / "inside.patch", "diff\n");
  aurpush::write_file(dir.parent_path() / "outside.patch", "diff\n");

  auto info = aurpush::parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source = inside.patch\n"
      "	source = ../outside.patch\n"
      "	source = /etc/hostname\n"
      "pkgname = foo\n");

  const auto files = aurpush::aur_file_set(aurpush::Workspace(dir), info);
  REQUIRE(contains(files, "inside.patch"));
  REQUIRE(!contains(files, "../outside.patch"));
  REQUIRE(!contains(files, "/etc/hostname"));
  fs::remove(dir.parent_path() / "outside.patch");
  fs::remove_all(dir);
}

TEST(fileset_excludes_build_artifacts) {
  const auto dir = make_temp_dir();
  aurpush::write_file(dir / "PKGBUILD", "pkgname=foo\n");
  aurpush::write_file(dir / "foo-1.0-1-any.pkg.tar.zst", "binary\n");
  aurpush::write_file(dir / "foo-1.0-1.src.tar.gz", "binary\n");
  aurpush::write_file(dir / "PKGBUILD~", "backup\n");
  fs::create_directories(dir / "pkg");
  aurpush::write_file(dir / "pkg" / "thing", "built\n");

  auto info = aurpush::parse_srcinfo(
      "pkgbase = foo\n	pkgver = 1\n	pkgrel = 1\npkgname = foo\n");
  const auto files = aurpush::aur_file_set(aurpush::Workspace(dir), info);
  REQUIRE(contains(files, "PKGBUILD"));
  REQUIRE(!contains(files, "foo-1.0-1-any.pkg.tar.zst"));
  REQUIRE(!contains(files, "foo-1.0-1.src.tar.gz"));
  REQUIRE(!contains(files, "PKGBUILD~"));
  REQUIRE(!contains(files, "pkg/thing"));
  fs::remove_all(dir);
}

// The artifact patterns are matched against the file name, so a directory that
// happens to contain ".pkg.tar" does not exclude real packaging files.
TEST(fileset_artifact_patterns_are_scoped_to_the_file_name) {
  const auto dir = make_temp_dir();
  aurpush::write_file(dir / "PKGBUILD", "pkgname=foo\n");
  fs::create_directories(dir / "my.pkg.tar.notes");
  aurpush::write_file(dir / "my.pkg.tar.notes" / "keep.patch", "diff\n");

  auto info = aurpush::parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source = my.pkg.tar.notes/keep.patch\n"
      "pkgname = foo\n");
  const auto files = aurpush::aur_file_set(aurpush::Workspace(dir), info);
  REQUIRE(contains(files, "my.pkg.tar.notes/keep.patch"));
  fs::remove_all(dir);
}
