#include "test.hpp"

#include "aurpush/commands.hpp"
#include "aurpush/error.hpp"
#include "aurpush/git.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using aurpush::Config;
using aurpush::Error;
using aurpush::run;

namespace {

std::string unique_suffix() {
  static int n = 0;
  return std::to_string(getpid()) + "-" + std::to_string(++n);
}

fs::path temp_root() {
  auto dir = fs::temp_directory_path() / ("aurpush-itest-" + unique_suffix());
  fs::create_directories(dir);
  return dir;
}

void git_cmd(const fs::path& dir, const std::vector<std::string>& args) {
  std::vector<std::string> argv{"git"};
  argv.insert(argv.end(), args.begin(), args.end());
  auto result = run(argv, dir, {{"GIT_TERMINAL_PROMPT", "0"}});
  if (!result.ok()) {
    throw std::runtime_error("git failed: " + result.err + result.out);
  }
}

void write_pkg(const fs::path& dir, const std::string& name, const std::string& ver,
               const std::string& rel, const std::string& extra_source = "") {
  std::string pb;
  pb += "pkgname=" + name + "\n";
  pb += "pkgver=" + ver + "\n";
  pb += "pkgrel=" + rel + "\n";
  pb += "pkgdesc='test package'\n";
  pb += "arch=('any')\n";
  pb += "license=('MIT')\n";
  if (!extra_source.empty()) {
    pb += "source=('" + extra_source + "')\n";
  }
  aurpush::write_file(dir / "PKGBUILD", pb);

  std::string si;
  si += "pkgbase = " + name + "\n";
  si += "\tpkgver = " + ver + "\n";
  si += "\tpkgrel = " + rel + "\n";
  si += "\tpkgdesc = test package\n";
  si += "\tarch = any\n";
  si += "\tlicense = MIT\n";
  if (!extra_source.empty()) {
    si += "\tsource = " + extra_source + "\n";
  }
  si += "\n";
  si += "pkgname = " + name + "\n";
  aurpush::write_file(dir / ".SRCINFO", si);
}

void identity(const fs::path& dir) {
  git_cmd(dir, {"config", "user.name", "Test User"});
  git_cmd(dir, {"config", "user.email", "test@example.com"});
}

fs::path make_bare(const fs::path& root, const std::string& pkgbase) {
  const auto bare = root / (pkgbase + ".git");
  fs::create_directories(bare);
  git_cmd(bare, {"init", "--bare", "-b", "master"});
  return bare;
}

Config make_cfg(const fs::path& pkg, const fs::path& bare) {
  Config cfg;
  cfg.cwd = pkg;
  cfg.remote_url = "file://" + bare.string();
  cfg.skip_ssh = true;
  return cfg;
}

struct Mute {
  std::ostringstream sink;
  std::streambuf* old;
  Mute() : old(std::cout.rdbuf(sink.rdbuf())) {}
  ~Mute() { std::cout.rdbuf(old); }
  std::string str() const { return sink.str(); }
};

std::string slurp_status(const Config& cfg) {
  Mute mute;
  aurpush::run_status(cfg);
  return mute.str();
}

}  // namespace

TEST(status_missing_pkgbuild) {
  const auto root = temp_root();
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  const auto text = slurp_status(cfg);
  REQUIRE(text.find("not found") != std::string::npos);
  {
    Mute mute;
    REQUIRE(aurpush::run_status(cfg, false) == 0);
    REQUIRE(aurpush::run_status(cfg, true) == 1);
  }
  fs::remove_all(root);
}

TEST(status_uninitialized) {
  const auto root = temp_root();
  write_pkg(root, "sample", "1.0.0", "1");
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  const auto text = slurp_status(cfg);
  REQUIRE(text.find("Package: sample") != std::string::npos);
  REQUIRE(text.find("not initialized") != std::string::npos);
  REQUIRE(text.find("aurpush init") != std::string::npos);
  {
    Mute mute;
    REQUIRE(aurpush::run_status(cfg, true) == 1);
  }
  fs::remove_all(root);
}

TEST(init_new_package_and_publish) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  const auto cfg = make_cfg(pkg, bare);

  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  REQUIRE(aurpush::file_exists(pkg / ".aurpush"));

  identity(pkg);
  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "Initial release") == 0);
  }

  const auto remote_sha = aurpush::git::ls_remote_master(cfg.remote_url);
  REQUIRE(!remote_sha.empty());

  const auto text = slurp_status(cfg);
  REQUIRE(text.find("initialized") != std::string::npos);
  REQUIRE(text.find("exists") != std::string::npos);
  {
    Mute mute;
    REQUIRE(aurpush::run_status(cfg, true) == 0);
  }

  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "again") == 0);
  }

  fs::remove_all(root);
}

TEST(status_lists_unpublished_files) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  const auto text = slurp_status(cfg);
  REQUIRE(text.find("unpublished file") != std::string::npos);
  REQUIRE(text.find("added") != std::string::npos);
  REQUIRE(text.find("PKGBUILD") != std::string::npos);
  REQUIRE(!fs::exists(pkg / "src"));
  fs::remove_all(root);
}

TEST(init_nested_pkgbuild_does_not_touch_parent) {
  const auto root = temp_root();
  git_cmd(root, {"init", "-b", "main"});
  identity(root);
  git_cmd(root, {"commit", "--allow-empty", "-m", "parent"});

  const auto pkg = root / "packaging";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);

  REQUIRE(!aurpush::git::is_repo(pkg));
  REQUIRE(aurpush::git::is_repo(root));

  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }

  REQUIRE(aurpush::git::is_repo(pkg));
  REQUIRE(fs::exists(pkg / ".git"));
  REQUIRE(aurpush::file_exists(pkg / ".aurpush"));
  REQUIRE_EQ(aurpush::git::current_branch(root), "main");
  REQUIRE(aurpush::git::remotes(root).empty());
  REQUIRE(aurpush::git::remote_url(pkg, "aur").has_value());
  fs::remove_all(root);
}

TEST(init_refuses_foreign_git_repo) {
  const auto root = temp_root();
  write_pkg(root, "sample", "1.0.0", "1");
  git_cmd(root, {"init", "-b", "main"});
  git_cmd(root, {"remote", "add", "origin", "https://github.com/example/sample.git"});
  identity(root);

  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  cfg.remote_url = "file://" + (root / "sample.git").string();

  bool threw = false;
  try {
    aurpush::run_init(cfg);
  } catch (const Error& e) {
    threw = true;
    REQUIRE(std::string(e.what()).find("mix AUR history") != std::string::npos);
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(publish_refuses_uninitialized) {
  const auto root = temp_root();
  write_pkg(root, "sample", "1.0.0", "1");
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  bool threw = false;
  try {
    aurpush::run_publish(cfg, "nope");
  } catch (const Error&) {
    threw = true;
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(publish_refuses_when_remote_ahead) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  identity(pkg);
  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "Initial release") == 0);
  }

  const auto other = root / "other";
  git_cmd(root, {"clone", cfg.remote_url, other.string()});
  identity(other);
  write_pkg(other, "sample", "1.0.1", "1");
  git_cmd(other, {"add", "-A"});
  git_cmd(other, {"commit", "-m", "remote update"});
  git_cmd(other, {"push", "origin", "HEAD:master"});

  write_pkg(pkg, "sample", "1.0.2", "1");
  bool threw = false;
  try {
    Mute mute;
    aurpush::run_publish(cfg, "local update");
  } catch (const Error& e) {
    threw = true;
    REQUIRE(std::string(e.what()).find("aurpush sync") != std::string::npos);
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(init_adopts_existing_without_clobbering_pkgbuild) {
  const auto root = temp_root();
  const auto donor = root / "donor";
  fs::create_directories(donor);
  write_pkg(donor, "sample", "1.0.0", "1");
  aurpush::write_file(donor / "extra.patch", "patch\n");
  git_cmd(donor, {"init", "-b", "master"});
  identity(donor);
  git_cmd(donor, {"add", "PKGBUILD", ".SRCINFO", "extra.patch"});
  git_cmd(donor, {"commit", "-m", "upstream"});

  const auto bare = make_bare(root, "sample");
  git_cmd(donor, {"remote", "add", "origin", "file://" + bare.string()});
  git_cmd(donor, {"push", "origin", "HEAD:master"});

  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.1.0", "1");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }

  const auto pb = aurpush::read_file(pkg / "PKGBUILD");
  REQUIRE(pb.find("pkgver=1.1.0") != std::string::npos);
  REQUIRE(aurpush::file_exists(pkg / "extra.patch"));
  fs::remove_all(root);
}

TEST(status_reports_outdated_srcinfo) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }

  const auto src_time = fs::last_write_time(pkg / ".SRCINFO");
  fs::last_write_time(pkg / "PKGBUILD", src_time + std::chrono::seconds(2));

  const auto text = slurp_status(cfg);
  REQUIRE(text.find("outdated") != std::string::npos);
  fs::remove_all(root);
}

TEST(sync_refuses_uninitialized) {
  const auto root = temp_root();
  write_pkg(root, "sample", "1.0.0", "1");
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  bool threw = false;
  try {
    aurpush::run_sync(cfg);
  } catch (const Error&) {
    threw = true;
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(sync_fast_forwards_when_behind) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  identity(pkg);
  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "Initial release") == 0);
  }

  const auto other = root / "other";
  git_cmd(root, {"clone", cfg.remote_url, other.string()});
  identity(other);
  write_pkg(other, "sample", "1.0.1", "1");
  git_cmd(other, {"add", "-A"});
  git_cmd(other, {"commit", "-m", "remote update"});
  git_cmd(other, {"push", "origin", "HEAD:master"});

  {
    Mute mute;
    REQUIRE(aurpush::run_sync(cfg) == 0);
  }
  const auto remote_sha = aurpush::git::ls_remote_master(cfg.remote_url);
  const auto local = aurpush::git::rev_parse(pkg, "HEAD");
  REQUIRE(local.has_value());
  REQUIRE_EQ(*local, remote_sha);

  const auto text = slurp_status(cfg);
  REQUIRE(text.find("up to date") != std::string::npos);
  fs::remove_all(root);
}

TEST(sync_noop_when_ahead_or_equal) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  identity(pkg);
  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "Initial release") == 0);
  }
  const auto after_publish = aurpush::git::rev_parse(pkg, "HEAD");
  {
    Mute mute;
    REQUIRE(aurpush::run_sync(cfg) == 0);
  }
  REQUIRE_EQ(*aurpush::git::rev_parse(pkg, "HEAD"), *after_publish);

  write_pkg(pkg, "sample", "1.0.1", "1");
  git_cmd(pkg, {"add", "-A"});
  git_cmd(pkg, {"commit", "-m", "local only"});
  const auto ahead = aurpush::git::rev_parse(pkg, "HEAD");
  {
    Mute mute;
    REQUIRE(aurpush::run_sync(cfg) == 0);
  }
  REQUIRE_EQ(*aurpush::git::rev_parse(pkg, "HEAD"), *ahead);
  fs::remove_all(root);
}

TEST(sync_refuses_diverged) {
  const auto root = temp_root();
  const auto pkg = root / "pkg";
  fs::create_directories(pkg);
  write_pkg(pkg, "sample", "1.0.0", "1");
  const auto bare = make_bare(root, "sample");
  auto cfg = make_cfg(pkg, bare);
  {
    Mute mute;
    REQUIRE(aurpush::run_init(cfg) == 0);
  }
  identity(pkg);
  {
    Mute mute;
    REQUIRE(aurpush::run_publish(cfg, "Initial release") == 0);
  }

  const auto other = root / "other";
  git_cmd(root, {"clone", cfg.remote_url, other.string()});
  identity(other);
  write_pkg(other, "sample", "1.0.1", "1");
  git_cmd(other, {"add", "-A"});
  git_cmd(other, {"commit", "-m", "remote update"});
  git_cmd(other, {"push", "origin", "HEAD:master"});

  write_pkg(pkg, "sample", "1.0.2", "1");
  git_cmd(pkg, {"add", "-A"});
  git_cmd(pkg, {"commit", "-m", "local update"});

  bool threw = false;
  try {
    Mute mute;
    aurpush::run_sync(cfg);
  } catch (const Error& e) {
    threw = true;
    REQUIRE(std::string(e.what()).find("diverged") != std::string::npos);
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(install_refuses_without_pkgbuild) {
  const auto root = temp_root();
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  bool threw = false;
  try {
    aurpush::run_install(cfg);
  } catch (const Error&) {
    threw = true;
  }
  REQUIRE(threw);
  fs::remove_all(root);
}

TEST(install_runs_makepkg_with_pkgbuild) {
  const auto root = temp_root();
  write_pkg(root, "sample", "1.0.0", "1");
  Config cfg;
  cfg.cwd = root;
  cfg.skip_ssh = true;
  Mute mute;
  REQUIRE(aurpush::run_install(cfg) == 0);
  fs::remove_all(root);
}
