#include "test.hpp"

#include "aurpush/cli.hpp"
#include "aurpush/error.hpp"

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
