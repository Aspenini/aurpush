#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aurpush {

struct ProcessOptions {
  std::filesystem::path cwd;
  std::vector<std::pair<std::string, std::string>> env;
  // Zero disables the timeout. A timed-out child is terminated along with its
  // process group, so helpers that spawn their own children (git -> ssh) die too.
  std::chrono::milliseconds timeout{0};
};

struct ProcessResult {
  int exit_code = -1;
  std::string out;
  std::string err;
  bool timed_out = false;

  bool ok() const { return exit_code == 0 && !timed_out; }
};

ProcessResult run(const std::vector<std::string>& argv, const ProcessOptions& opts);

ProcessResult run(const std::vector<std::string>& argv,
                  const std::filesystem::path& cwd = {},
                  const std::vector<std::pair<std::string, std::string>>& env = {});

int run_foreground(const std::vector<std::string>& argv,
                   const std::filesystem::path& cwd = {});

std::optional<std::filesystem::path> find_in_path(std::string_view program);

// Throws a single Error naming every missing program, so the user does not
// discover them one failed run at a time.
void require_tools(const std::vector<std::string>& programs);

}  // namespace aurpush
