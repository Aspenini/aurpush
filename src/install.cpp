#include "aurpush/commands.hpp"

#include "aurpush/error.hpp"
#include "aurpush/process.hpp"
#include "aurpush/util.hpp"

#include <iostream>

namespace aurpush {

int run_install(const Config& cfg) {
  const auto& dir = cfg.cwd;
  if (!file_exists(dir / "PKGBUILD")) {
    throw Error("no PKGBUILD in the current directory");
  }

  std::cout << "Building and installing with makepkg -si...\n";
  const int code = run_foreground({"makepkg", "-si"}, dir);
  if (code != 0) {
    return code > 0 ? code : 2;
  }
  return 0;
}

}  // namespace aurpush
