#pragma once

#include <string>
#include <vector>

namespace aurpush {

struct SshAuth {
  bool ok = false;
  std::string username;
  std::string error;
};

struct AurRepos {
  bool ok = false;
  std::vector<std::string> names;
  std::string error;
};

SshAuth check_aur_ssh();
AurRepos list_aur_repos();

}  // namespace aurpush
