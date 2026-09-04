#include "aurpush/commands.hpp"

#include "aurpush/error.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <iostream>

namespace aurpush {

int run_install(const Config& cfg, const std::vector<std::string>& makepkg_args) {
  const auto& dir = cfg.cwd;
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }
  require_tools({"makepkg"});

  std::vector<std::string> argv = {"makepkg", "-si"};
  argv.insert(argv.end(), makepkg_args.begin(), makepkg_args.end());

  std::cout << "Building and installing with " << join(argv, " ") << "...\n";
  const int code = run_foreground(argv, dir);
  if (code != 0) {
    // makepkg has already explained itself on the terminal; keep its status but
    // never report success on a zero-ish sentinel.
    throw Error("makepkg failed", code > 0 ? code : 2);
  }
  return 0;
}

}  // namespace aurpush
