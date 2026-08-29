#include "aurpush/cli.hpp"

#include "aurpush/error.hpp"

#include <ostream>

namespace aurpush {
namespace {

std::string usage_text() {
  return "Usage: aurpush [init|sync]\n"
         "       aurpush -m <message>\n"
         "       aurpush --check\n"
         "\n"
         "Inspect, initialize, sync, or publish the AUR package in the current directory.\n"
         "\n"
         "Commands:\n"
         "  (no arguments)         Show workspace status (read-only)\n"
         "  init                   Initialize this directory as an AUR workspace\n"
         "  sync                   Fast-forward to the AUR remote (never force)\n"
         "  -m, --message <msg>    Publish with the given commit message\n"
         "\n"
         "Options:\n"
         "  --check                Exit non-zero if any status check failed\n"
         "  -h, --help             Show this help\n"
         "  -V, --version          Show version";
}

}  // namespace

void print_help(std::ostream& out) { out << usage_text() << '\n'; }

void print_version(std::ostream& out) { out << "aurpush " << AURPUSH_VERSION << '\n'; }

Options parse_args(const std::vector<std::string>& args) {
  Options opt;
  bool saw_init = false;
  bool saw_sync = false;
  bool saw_message = false;
  bool saw_check = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-h" || a == "--help") {
      opt.command = Command::Help;
      return opt;
    }
    if (a == "-V" || a == "--version") {
      opt.command = Command::Version;
      return opt;
    }
    if (a == "--check") {
      if (saw_check) {
        throw Error("unexpected extra argument: --check");
      }
      saw_check = true;
      continue;
    }
    if (a == "init") {
      if (saw_init) {
        throw Error("unexpected extra argument: init");
      }
      saw_init = true;
      continue;
    }
    if (a == "sync") {
      if (saw_sync) {
        throw Error("unexpected extra argument: sync");
      }
      saw_sync = true;
      continue;
    }
    if (a == "-m" || a == "--message") {
      if (i + 1 >= args.size()) {
        throw Error("option " + a + " requires a commit message");
      }
      opt.message = args[++i];
      saw_message = true;
      continue;
    }
    if (a.rfind("--message=", 0) == 0) {
      opt.message = a.substr(10);
      saw_message = true;
      continue;
    }
    if (a.size() > 2 && a[0] == '-' && a[1] == 'm') {
      opt.message = a.substr(2);
      saw_message = true;
      continue;
    }
    throw Error("unknown argument: " + a + "\nTry 'aurpush --help' for usage.");
  }

  if (saw_init && saw_message) {
    throw Error("init and -m cannot be used together");
  }
  if (saw_sync && saw_message) {
    throw Error("sync and -m cannot be used together");
  }
  if (saw_init && saw_sync) {
    throw Error("init and sync cannot be used together");
  }
  if (saw_check && saw_init) {
    throw Error("--check cannot be used with init");
  }
  if (saw_check && saw_sync) {
    throw Error("--check cannot be used with sync");
  }
  if (saw_check && saw_message) {
    throw Error("--check cannot be used with -m");
  }
  if (saw_message && opt.message.empty()) {
    throw Error("commit message must not be empty");
  }
  if (saw_init) {
    opt.command = Command::Init;
  } else if (saw_sync) {
    opt.command = Command::Sync;
  } else if (saw_message) {
    opt.command = Command::Publish;
  }
  opt.check = saw_check;
  return opt;
}

Options parse_args(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i] ? argv[i] : "");
  }
  return parse_args(args);
}

}  // namespace aurpush
