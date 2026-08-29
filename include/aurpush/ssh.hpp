#pragma once

#include <string>
#include <vector>

namespace aurpush {

struct SshAuth {
  bool ok = false;
  std::string username;
  std::string error;
};

SshAuth check_aur_ssh();
std::vector<std::string> list_aur_repos();

}  // namespace aurpush
