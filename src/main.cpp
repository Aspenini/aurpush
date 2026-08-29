#include "aurpush/cli.hpp"
#include "aurpush/commands.hpp"
#include "aurpush/config.hpp"
#include "aurpush/error.hpp"

#include <iostream>

int main(int argc, char** argv) {
  try {
    const auto opt = aurpush::parse_args(argc, argv);
    switch (opt.command) {
      case aurpush::Command::Help:
        aurpush::print_help(std::cout);
        return 0;
      case aurpush::Command::Version:
        aurpush::print_version(std::cout);
        return 0;
      case aurpush::Command::Status:
        return aurpush::run_status(aurpush::config_from_env(), opt.check);
      case aurpush::Command::Init:
        return aurpush::run_init(aurpush::config_from_env());
      case aurpush::Command::Publish:
        return aurpush::run_publish(aurpush::config_from_env(), opt.message);
    }
  } catch (const aurpush::Error& e) {
    std::cerr << "error: " << e.what() << '\n';
    return e.code();
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 2;
  }
}
