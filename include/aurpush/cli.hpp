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
};

Options parse_args(const std::vector<std::string>& args);
Options parse_args(int argc, char** argv);

void print_help(std::ostream& out);
void print_version(std::ostream& out);

}  // namespace aurpush
