#pragma once

#include <stdexcept>
#include <string>

namespace aurpush {

class Error : public std::runtime_error {
 public:
  explicit Error(const std::string& message, int code = 1)
      : std::runtime_error(message), code_(code) {}

  int code() const noexcept { return code_; }

 private:
  int code_;
};

}  // namespace aurpush
