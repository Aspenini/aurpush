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

// Cached for the lifetime of the process: each is one network round trip.
const SshAuth& check_aur_ssh();
const AurRepos& list_aur_repos();

}  // namespace aurpush
