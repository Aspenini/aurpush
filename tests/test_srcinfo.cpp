#include "test.hpp"

#include "aurpush/srcinfo.hpp"

using aurpush::is_remote_source;
using aurpush::parse_srcinfo;
using aurpush::source_local_path;

TEST(srcinfo_basic) {
  const auto info = parse_srcinfo(
      "pkgbase = aurpush\n"
      "	pkgver = 0.1.0\n"
      "	pkgrel = 1\n"
      "	pkgdesc = A tool\n"
      "	source = https://example.com/aurpush-0.1.0.tar.gz\n"
      "	source = example.patch\n"
      "	install = aurpush.install\n"
      "\n"
      "pkgname = aurpush\n");
  REQUIRE_EQ(info.pkgbase, "aurpush");
  REQUIRE_EQ(info.pkgver, "0.1.0");
  REQUIRE_EQ(info.pkgrel, "1");
  REQUIRE_EQ(info.version_string(), "0.1.0-1");
  REQUIRE(info.pkgnames.size() == 1);
  REQUIRE(info.sources.size() == 2);
  REQUIRE(info.install_files.size() == 1);
  REQUIRE_EQ(info.install_files[0], "aurpush.install");
}

TEST(srcinfo_epoch) {
  const auto info = parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 2.0\n"
      "	pkgrel = 3\n"
      "	epoch = 1\n"
      "pkgname = foo\n");
  REQUIRE_EQ(info.version_string(), "1:2.0-3");
}

TEST(srcinfo_split_packages) {
  const auto info = parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source = local.diff\n"
      "pkgname = foo\n"
      "	install = foo.install\n"
      "pkgname = foo-docs\n"
      "	install = docs.install\n");
  REQUIRE(info.pkgnames.size() == 2);
  REQUIRE(info.install_files.size() == 2);
}

TEST(srcinfo_arch_sources) {
  const auto info = parse_srcinfo(
      "pkgbase = foo\n"
      "	pkgver = 1\n"
      "	pkgrel = 1\n"
      "	source = common.patch\n"
      "	source_x86_64 = extra.patch\n"
      "	source_i686 = https://example.com/i686.tar.gz\n"
      "pkgname = foo\n");
  REQUIRE(info.sources.size() == 3);
  REQUIRE_EQ(info.sources[0], "common.patch");
  REQUIRE_EQ(info.sources[1], "extra.patch");
  REQUIRE_EQ(info.sources[2], "https://example.com/i686.tar.gz");
  REQUIRE(!is_remote_source(info.sources[1]));
  REQUIRE(is_remote_source(info.sources[2]));
  REQUIRE_EQ(source_local_path(info.sources[1]), "extra.patch");
  REQUIRE(source_local_path(info.sources[2]).empty());
}

TEST(source_remote_detection) {
  REQUIRE(is_remote_source("https://example.com/foo.tar.gz"));
  REQUIRE(is_remote_source("foo.tar.gz::https://example.com/foo.tar.gz"));
  REQUIRE(is_remote_source("git+https://github.com/foo/bar.git"));
  REQUIRE(!is_remote_source("example.patch"));
  REQUIRE(!is_remote_source("patches/foo.diff"));
  REQUIRE_EQ(source_local_path("example.patch"), "example.patch");
  REQUIRE_EQ(source_local_path("patches/foo.diff"), "patches/foo.diff");
  REQUIRE(source_local_path("https://example.com/foo.tar.gz").empty());
}
