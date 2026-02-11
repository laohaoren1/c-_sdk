#pragma once

#include <functional>
#include <iostream>
#include <string_view>

namespace linkerhand {

enum class LogLevel { kDebug, kInfo, kWarning, kError };

/// Logger callback type: receives level and message.
using LogCallback = std::function<void(LogLevel, std::string_view)>;

/// Default logger: writes to stderr with prefix.
inline LogCallback default_logger() {
  return [](LogLevel level, std::string_view msg) {
    const char* prefix = "INFO";
    switch (level) {
      case LogLevel::kDebug:   prefix = "DEBUG"; break;
      case LogLevel::kInfo:    prefix = "INFO"; break;
      case LogLevel::kWarning: prefix = "WARN"; break;
      case LogLevel::kError:   prefix = "ERROR"; break;
    }
    std::cerr << "[linkerhand:" << prefix << "] " << msg << "\n";
  };
}

/// No-op logger.
inline LogCallback null_logger() {
  return [](LogLevel, std::string_view) {};
}

}  // namespace linkerhand
