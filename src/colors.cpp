#include "aurpush/colors.hpp"

#include "aurpush/util.hpp"

#include <iomanip>
#include <sstream>

namespace aurpush {

Glyphs glyphs() {
  Glyphs g;
  const bool tty = stdout_is_tty();
  const bool utf8 = looks_like_utf8_locale();
  if (tty) {
    g.green = "\033[32m";
    g.red = "\033[31m";
    g.yellow = "\033[33m";
    g.reset = "\033[0m";
  }
  if (tty && utf8) {
    g.ok = "✓";
    g.fail = "✗";
    g.warn = "!";
  }
  return g;
}

std::string format_check(const std::string& label, CheckKind kind,
                         const std::string& detail) {
  const Glyphs g = glyphs();
  std::ostringstream ss;
  ss << std::left << std::setw(16) << label;
  switch (kind) {
    case CheckKind::Ok:
      ss << g.green << g.ok << g.reset << ' ' << detail;
      break;
    case CheckKind::Fail:
      ss << g.red << g.fail << g.reset << ' ' << detail;
      break;
    case CheckKind::Warn:
      ss << g.yellow << g.warn << g.reset << ' ' << detail;
      break;
    case CheckKind::Plain:
      ss << detail;
      break;
  }
  return ss.str();
}

}  // namespace aurpush
