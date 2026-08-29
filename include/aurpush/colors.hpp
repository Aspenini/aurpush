#pragma once

#include <string>

namespace aurpush {

enum class CheckKind { Ok, Fail, Warn, Plain };

struct Glyphs {
  const char* ok = "OK";
  const char* fail = "FAIL";
  const char* warn = "WARN";
  const char* green = "";
  const char* red = "";
  const char* yellow = "";
  const char* reset = "";
};

Glyphs glyphs();

std::string format_check(const std::string& label, CheckKind kind,
                         const std::string& detail);

}  // namespace aurpush
