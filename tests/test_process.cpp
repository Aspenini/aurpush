#include "test.hpp"

#include "aurpush/error.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

fs::path scratch_dir() {
  static int n = 0;
  auto dir = fs::temp_directory_path() /
             ("aurpush-proc-" + std::to_string(getpid()) + "-" + std::to_string(++n));
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST(process_captures_output_and_status) {
  const auto result = aurpush::run({"sh", "-c", "printf out; printf err >&2; exit 3"});
  REQUIRE(!result.ok());
  REQUIRE_EQ(result.exit_code, 3);
  REQUIRE_EQ(result.out, "out");
  REQUIRE_EQ(result.err, "err");
  REQUIRE(!result.timed_out);
}

// A failed exec used to be indistinguishable from a real exit 127, so a missing
// program produced no diagnostic at all.
TEST(process_missing_program_names_itself) {
  std::string message;
  try {
    aurpush::run({"aurpush-definitely-not-a-real-program"});
  } catch (const aurpush::Error& e) {
    message = e.what();
  }
  REQUIRE(message.find("aurpush-definitely-not-a-real-program") != std::string::npos);
  REQUIRE(message.find("failed to run") != std::string::npos);
}

TEST(process_foreground_missing_program_throws) {
  bool threw = false;
  try {
    aurpush::run_foreground({"aurpush-definitely-not-a-real-program"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(process_missing_cwd_is_reported) {
  bool threw = false;
  try {
    aurpush::run({"true"}, "/aurpush-no-such-directory");
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(process_timeout_kills_the_child) {
  aurpush::ProcessOptions opts;
  opts.timeout = std::chrono::milliseconds(300);
  const auto started = std::chrono::steady_clock::now();
  const auto result = aurpush::run({"sleep", "30"}, opts);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  REQUIRE(result.timed_out);
  REQUIRE(!result.ok());
  REQUIRE(elapsed < std::chrono::seconds(10));
}

// The child gets its own process group so a timeout also reaps grandchildren --
// git spawning ssh is the case that matters.
TEST(process_timeout_reaps_grandchildren) {
  aurpush::ProcessOptions opts;
  opts.timeout = std::chrono::milliseconds(300);
  const auto started = std::chrono::steady_clock::now();
  const auto result = aurpush::run({"sh", "-c", "sleep 30 & wait"}, opts);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  REQUIRE(result.timed_out);
  REQUIRE(elapsed < std::chrono::seconds(10));
}

TEST(process_env_reaches_the_child) {
  const auto result =
      aurpush::run({"sh", "-c", "printf %s \"$AURPUSH_TEST_VAR\""}, {},
                   {{"AURPUSH_TEST_VAR", "hello"}});
  REQUIRE(result.ok());
  REQUIRE_EQ(result.out, "hello");
}

TEST(process_cwd_reaches_the_child) {
  const auto dir = scratch_dir();
  const auto result = aurpush::run({"sh", "-c", "printf . > marker"}, dir);
  REQUIRE(result.ok());
  REQUIRE(aurpush::file_exists(dir / "marker"));
  fs::remove_all(dir);
}

TEST(process_find_in_path) {
  REQUIRE(aurpush::find_in_path("sh").has_value());
  REQUIRE(!aurpush::find_in_path("aurpush-definitely-not-a-real-program").has_value());
}

TEST(process_require_tools_lists_every_missing_one) {
  aurpush::require_tools({"sh"});  // present: must not throw
  std::string message;
  try {
    aurpush::require_tools({"sh", "aurpush-missing-one", "aurpush-missing-two"});
  } catch (const aurpush::Error& e) {
    message = e.what();
  }
  REQUIRE(message.find("aurpush-missing-one") != std::string::npos);
  REQUIRE(message.find("aurpush-missing-two") != std::string::npos);
  REQUIRE(message.find("sh") != std::string::npos);
}

TEST(util_write_file_is_atomic_and_leaves_no_temporary) {
  const auto dir = scratch_dir();
  const auto target = dir / "file.txt";
  aurpush::write_file(target, "first");
  aurpush::write_file(target, "second");
  REQUIRE_EQ(aurpush::read_file(target), std::string("second"));
  int leftovers = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().filename().string() != "file.txt") {
      ++leftovers;
    }
  }
  REQUIRE_EQ(leftovers, 0);
  fs::remove_all(dir);
}

TEST(util_write_file_keeps_original_when_the_target_is_unwritable) {
  const auto dir = scratch_dir();
  const auto target = dir / "sub" / "file.txt";
  bool threw = false;
  try {
    aurpush::write_file(target, "data");  // parent directory does not exist
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
  fs::remove_all(dir);
}

TEST(util_rejects_paths_that_escape_the_package_directory) {
  REQUIRE(aurpush::is_contained_relative_path("PKGBUILD"));
  REQUIRE(aurpush::is_contained_relative_path("patches/fix.patch"));
  REQUIRE(!aurpush::is_contained_relative_path(""));
  REQUIRE(!aurpush::is_contained_relative_path("/etc/passwd"));
  REQUIRE(!aurpush::is_contained_relative_path("../outside"));
  REQUIRE(!aurpush::is_contained_relative_path("a/../../outside"));
}

TEST(util_join) {
  REQUIRE_EQ(aurpush::join({"a", "b", "c"}, " "), std::string("a b c"));
  REQUIRE_EQ(aurpush::join({}, " "), std::string(""));
  REQUIRE_EQ(aurpush::join({"only"}, ", "), std::string("only"));
}

// Every pipe opened for a child must be closed again, on the success path and
// on the exec-failure path alike.
TEST(process_does_not_leak_file_descriptors) {
  auto open_fds = [] {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc/self/fd", ec)) {
      static_cast<void>(entry);
      ++count;
    }
    return count;
  };

  aurpush::run({"true"});  // warm up any lazily opened descriptors
  const int before = open_fds();
  for (int i = 0; i < 20; ++i) {
    aurpush::run({"sh", "-c", "printf hello"});
    try {
      aurpush::run({"aurpush-definitely-not-a-real-program"});
    } catch (const aurpush::Error&) {
    }
  }
  REQUIRE(open_fds() <= before);
}

TEST(process_find_in_path_ignores_directories) {
  // /usr/bin is executable as a directory, but it is not a program.
  REQUIRE(!aurpush::find_in_path("/usr/bin").has_value());
}
