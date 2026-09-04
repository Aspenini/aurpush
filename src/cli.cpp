#include "aurpush/cli.hpp"

#include "aurpush/error.hpp"

#include <optional>
#include <ostream>
#include <string_view>

namespace aurpush {
namespace {

std::string usage_text() {
  return "Usage: aurpush [init|sync|install]\n"
         "       aurpush -m <message>\n"
         "       aurpush --check\n"
         "\n"
         "Inspect, initialize, sync, install, or publish the AUR package in the current directory.\n"
         "\n"
         "Commands:\n"
         "  (no arguments)         Show workspace status (read-only)\n"
         "  init                   Initialize this directory as an AUR workspace\n"
         "  sync                   Fast-forward to the AUR remote (never force)\n"
         "  install                Build and install locally with makepkg -si\n"
         "  -m, --message <msg>    Publish with the given commit message\n"
         "\n"
         "Options:\n"
         "  --check                Exit non-zero if any status check failed\n"
         "  --dry-run              With -m, report what would be published and stop\n"
         "  --no-color             Disable coloured output (see also NO_COLOR)\n"
         "  -h, --help             Show this help\n"
         "  -V, --version          Show version\n"
         "\n"
         "Anything after `--` is passed through to makepkg by `install`:\n"
         "  aurpush install -- --noconfirm --needed";
}

const char* command_name(Command command) {
  switch (command) {
    case Command::Init:
      return "init";
    case Command::Sync:
      return "sync";
    case Command::Install:
      return "install";
    case Command::Publish:
      return "-m";
    case Command::Status:
    case Command::Help:
    case Command::Version:
      break;
  }
  return "status";
}

// One command per invocation: every combination is rejected by the same rule,
// so adding a command does not mean adding a check against each existing one.
void select(std::optional<Command>& chosen, Command command) {
  if (chosen && *chosen != command) {
    throw Error(std::string(command_name(*chosen)) + " and " + command_name(command) +
                " cannot be used together");
  }
  if (chosen && *chosen == command) {
    throw Error(std::string("unexpected extra argument: ") + command_name(command));
  }
  chosen = command;
}

void set_message(Options& opt, bool& saw_message, std::string value) {
  if (saw_message) {
    throw Error("commit message given more than once");
  }
  saw_message = true;
  opt.message = std::move(value);
}

}  // namespace

void print_help(std::ostream& out) { out << usage_text() << '\n'; }

void print_version(std::ostream& out) { out << "aurpush " << AURPUSH_VERSION << '\n'; }

Options parse_args(const std::vector<std::string>& args) {
  Options opt;
  std::optional<Command> chosen;
  bool saw_message = false;
  bool saw_check = false;
  bool saw_dry_run = false;
  bool saw_separator = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];

    if (saw_separator) {
      opt.makepkg_args.push_back(a);
      continue;
    }
    if (a == "--") {
      saw_separator = true;
      continue;
    }
    if (a == "-h" || a == "--help") {
      opt.command = Command::Help;
      return opt;
    }
    if (a == "-V" || a == "--version") {
      opt.command = Command::Version;
      return opt;
    }
    if (a == "--no-color" || a == "--no-colour") {
      opt.no_color = true;
      continue;
    }
    if (a == "--check") {
      if (saw_check) {
        throw Error("unexpected extra argument: --check");
      }
      saw_check = true;
      continue;
    }
    if (a == "--dry-run") {
      if (saw_dry_run) {
        throw Error("unexpected extra argument: --dry-run");
      }
      saw_dry_run = true;
      continue;
    }
    if (a == "init") {
      select(chosen, Command::Init);
      continue;
    }
    if (a == "sync") {
      select(chosen, Command::Sync);
      continue;
    }
    if (a == "install") {
      select(chosen, Command::Install);
      continue;
    }
    if (a == "-m" || a == "--message") {
      if (i + 1 >= args.size()) {
        throw Error("option " + a + " requires a commit message");
      }
      set_message(opt, saw_message, args[++i]);
      select(chosen, Command::Publish);
      continue;
    }
    if (a.starts_with("--message=")) {
      set_message(opt, saw_message, a.substr(std::string_view("--message=").size()));
      select(chosen, Command::Publish);
      continue;
    }
    if (a.size() > 2 && a.starts_with("-m")) {
      set_message(opt, saw_message, a.substr(2));
      select(chosen, Command::Publish);
      continue;
    }
    throw Error("unknown argument: " + a + "\nTry 'aurpush --help' for usage.");
  }

  opt.command = chosen.value_or(Command::Status);

  if (saw_check && opt.command != Command::Status) {
    throw Error(std::string("--check cannot be used with ") + command_name(opt.command));
  }
  if (saw_dry_run && opt.command != Command::Publish) {
    throw Error(std::string("--dry-run cannot be used with ") + command_name(opt.command));
  }
  if (!opt.makepkg_args.empty() && opt.command != Command::Install) {
    throw Error("arguments after `--` are only accepted by `aurpush install`");
  }
  if (saw_message && opt.message.empty()) {
    throw Error("commit message must not be empty");
  }
  opt.check = saw_check;
  opt.dry_run = saw_dry_run;
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
