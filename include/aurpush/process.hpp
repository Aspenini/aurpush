#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace aurpush {

struct ProcessResult {
  int exit_code = -1;
  std::string out;
  std::string err;

  bool ok() const { return exit_code == 0; }
};

ProcessResult run(const std::vector<std::string>& argv,
                  const std::filesystem::path& cwd = {},
                  const std::vector<std::pair<std::string, std::string>>& env = {});

int run_foreground(const std::vector<std::string>& argv,
                   const std::filesystem::path& cwd = {});

}  // namespace aurpush
