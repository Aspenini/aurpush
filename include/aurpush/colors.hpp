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

// Turns colour off for the rest of the run. Must be called before any output;
// main() applies --no-color here right after parsing.
void disable_color();

// Probed once per run: stdout being a TTY, TERM, NO_COLOR, and the locale.
const Glyphs& glyphs();

std::string format_check(const std::string& label, CheckKind kind,
                         const std::string& detail);

}  // namespace aurpush
