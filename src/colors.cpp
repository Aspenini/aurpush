#include "aurpush/colors.hpp"

#include "aurpush/util.hpp"

#include <iomanip>
#include <sstream>

namespace aurpush {
namespace {

bool g_forced_off = false;

bool color_allowed() {
  if (g_forced_off) {
    return false;
  }
  // https://no-color.org: any non-empty value opts out.
  if (env_var("NO_COLOR")) {
    return false;
  }
  if (const auto term = env_var("TERM"); term && *term == "dumb") {
    return false;
  }
  return stdout_is_tty();
}

Glyphs compute() {
  Glyphs g;
  const bool color = color_allowed();
  if (color) {
    g.green = "\033[32m";
    g.red = "\033[31m";
    g.yellow = "\033[33m";
    g.reset = "\033[0m";
  }
  if (color && looks_like_utf8_locale()) {
    g.ok = "✓";
    g.fail = "✗";
    g.warn = "!";
  }
  return g;
}

}  // namespace

void disable_color() { g_forced_off = true; }

const Glyphs& glyphs() {
  static const Glyphs g = compute();
  return g;
}

std::string format_check(const std::string& label, CheckKind kind,
                         const std::string& detail) {
  const Glyphs& g = glyphs();
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
