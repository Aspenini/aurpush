#include "test.hpp"

#include "aurpush/cli.hpp"
#include "aurpush/error.hpp"

#include <cstddef>

using aurpush::Command;
using aurpush::parse_args;

TEST(cli_default_is_status) {
  auto opt = parse_args({"aurpush"});
  REQUIRE(opt.command == Command::Status);
}

TEST(cli_init) {
  auto opt = parse_args({"aurpush", "init"});
  REQUIRE(opt.command == Command::Init);
}

TEST(cli_message_short) {
  auto opt = parse_args({"aurpush", "-m", "Update to 1.2.0"});
  REQUIRE(opt.command == Command::Publish);
  REQUIRE_EQ(opt.message, "Update to 1.2.0");
}

TEST(cli_message_long) {
  auto opt = parse_args({"aurpush", "--message", "hello"});
  REQUIRE(opt.command == Command::Publish);
  REQUIRE_EQ(opt.message, "hello");
}

TEST(cli_message_equals) {
  auto opt = parse_args({"aurpush", "--message=hello"});
  REQUIRE(opt.command == Command::Publish);
  REQUIRE_EQ(opt.message, "hello");
}

TEST(cli_sync) {
  auto opt = parse_args({"aurpush", "sync"});
  REQUIRE(opt.command == Command::Sync);
}

TEST(cli_install) {
  auto opt = parse_args({"aurpush", "install"});
  REQUIRE(opt.command == Command::Install);
}

TEST(cli_install_and_message_fail) {
  bool threw = false;
  try {
    parse_args({"aurpush", "install", "-m", "nope"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_sync_and_message_fail) {
  bool threw = false;
  try {
    parse_args({"aurpush", "sync", "-m", "nope"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_check) {
  auto opt = parse_args({"aurpush", "--check"});
  REQUIRE(opt.command == Command::Status);
  REQUIRE(opt.check);
}

TEST(cli_check_with_init_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "init", "--check"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_check_with_message_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "--check", "-m", "nope"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_help) {
  auto opt = parse_args({"aurpush", "--help"});
  REQUIRE(opt.command == Command::Help);
}

TEST(cli_version) {
  auto opt = parse_args({"aurpush", "-V"});
  REQUIRE(opt.command == Command::Version);
}

TEST(cli_empty_message_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "-m", ""});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_init_and_message_fail) {
  bool threw = false;
  try {
    parse_args({"aurpush", "init", "-m", "nope"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_unknown_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "push"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_repeated_message_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "-m", "first", "-m", "second"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_repeated_subcommand_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "init", "init"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_init_and_sync_fail) {
  bool threw = false;
  try {
    parse_args({"aurpush", "init", "sync"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_dry_run_with_message) {
  auto opt = parse_args({"aurpush", "-m", "hello", "--dry-run"});
  REQUIRE(opt.command == Command::Publish);
  REQUIRE(opt.dry_run);
}

TEST(cli_dry_run_without_message_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "--dry-run"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}

TEST(cli_no_color_flag) {
  auto opt = parse_args({"aurpush", "--no-color"});
  REQUIRE(opt.command == Command::Status);
  REQUIRE(opt.no_color);
}

TEST(cli_install_passthrough_args) {
  auto opt = parse_args({"aurpush", "install", "--", "--noconfirm", "-f"});
  REQUIRE(opt.command == Command::Install);
  REQUIRE_EQ(opt.makepkg_args.size(), std::size_t{2});
  REQUIRE_EQ(opt.makepkg_args[0], "--noconfirm");
  REQUIRE_EQ(opt.makepkg_args[1], "-f");
}

TEST(cli_passthrough_after_separator_is_not_parsed) {
  // `--check` past the separator is makepkg's argument, not aurpush's.
  auto opt = parse_args({"aurpush", "install", "--", "--check"});
  REQUIRE(opt.command == Command::Install);
  REQUIRE(!opt.check);
  REQUIRE_EQ(opt.makepkg_args[0], "--check");
}

TEST(cli_passthrough_without_install_fails) {
  bool threw = false;
  try {
    parse_args({"aurpush", "sync", "--", "-f"});
  } catch (const aurpush::Error&) {
    threw = true;
  }
  REQUIRE(threw);
}
