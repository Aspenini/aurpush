#include "test.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
  const char* old_path = std::getenv("PATH");
  std::string path = AURPUSH_FAKE_DIR;
  if (old_path && *old_path) {
    path += ":";
    path += old_path;
  }
  setenv("PATH", path.c_str(), 1);
  setenv("GIT_TERMINAL_PROMPT", "0", 1);

  int failed = 0;
  for (const auto& test : all_tests()) {
    try {
      test.fn();
      std::cout << "ok   " << test.name << '\n';
    } catch (const std::exception& e) {
      std::cerr << "FAIL " << test.name << ": " << e.what() << '\n';
      ++failed;
    }
  }
  if (failed != 0) {
    std::cerr << failed << " test(s) failed\n";
    return 1;
  }
  std::cout << all_tests().size() << " test(s) passed\n";
  return 0;
}
