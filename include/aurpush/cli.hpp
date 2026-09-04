#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace aurpush {

enum class Command { Status, Init, Sync, Install, Publish, Help, Version };

struct Options {
  Command command = Command::Status;
  std::string message;
  bool check = false;
  bool dry_run = false;
  bool no_color = false;
  // Arguments after `--`, forwarded verbatim to makepkg by `install`.
  std::vector<std::string> makepkg_args;
};

Options parse_args(const std::vector<std::string>& args);
Options parse_args(int argc, char** argv);

void print_help(std::ostream& out);
void print_version(std::ostream& out);

}  // namespace aurpush
