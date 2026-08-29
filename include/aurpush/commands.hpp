#pragma once

#include <string>

#include "aurpush/config.hpp"

namespace aurpush {

int run_status(const Config& cfg, bool check = false);
int run_init(const Config& cfg);
int run_sync(const Config& cfg);
int run_install(const Config& cfg);
int run_publish(const Config& cfg, const std::string& message);

}  // namespace aurpush
