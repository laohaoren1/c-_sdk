#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "linkerhand/exceptions.hpp"

namespace linkerhand::hand::detail {

inline double now_unix_seconds() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration<double>(now.time_since_epoch()).count();
}

}  // namespace linkerhand::hand::detail
