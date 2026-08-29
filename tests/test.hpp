#pragma once

#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& all_tests() {
  static std::vector<TestCase> tests;
  return tests;
}

#define TEST(name)                                                            \
  static void name();                                                         \
  static bool name##_reg = (all_tests().push_back({#name, name}), true);      \
  static void name()

#define REQUIRE(cond)                                                         \
  do {                                                                        \
    if (!(cond)) {                                                            \
      throw std::runtime_error(std::string(#cond) + " (" + __FILE__ + ":" +   \
                               std::to_string(__LINE__) + ")");               \
    }                                                                         \
  } while (0)

#define REQUIRE_EQ(a, b)                                                      \
  do {                                                                        \
    if (!((a) == (b))) {                                                      \
      throw std::runtime_error(std::string(#a " == " #b) + " (" + __FILE__ +  \
                               ":" + std::to_string(__LINE__) + ")");         \
    }                                                                         \
  } while (0)
