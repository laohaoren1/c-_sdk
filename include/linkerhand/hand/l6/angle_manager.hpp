#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "linkerhand/can_dispatcher.hpp"
#include "linkerhand/iterable_queue.hpp"
#include "linkerhand/lifecycle.hpp"

namespace linkerhand::hand::l6 {

struct AngleData {
  std::array<int, 6> angles{};
  double timestamp = 0.0;
};

class AngleManager {
 public:
  AngleManager(
      std::uint32_t arbitration_id,
      CANMessageDispatcher& dispatcher,
      std::shared_ptr<linkerhand::Lifecycle> lifecycle);
  ~AngleManager();

  AngleManager(const AngleManager&) = delete;
  AngleManager& operator=(const AngleManager&) = delete;

  /// Set angles. Accepts any contiguous range of 6 ints (array, vector, span).
  void set_angles(std::span<const int> angles);

  /// Blocking get with type-safe duration.
  [[nodiscard]] AngleData get_angles_blocking(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{100});

  /// Get cached latest value (non-blocking, no CAN I/O).
  [[nodiscard]] std::optional<AngleData> get_current_angles() const;

  /// Start streaming. Returns a queue that auto-closes when stop_streaming() is called.
  [[nodiscard]] IterableQueue<AngleData> stream(
      std::chrono::milliseconds interval = std::chrono::milliseconds{100},
      std::size_t maxsize = 100);

  void stop_streaming();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle_;
};

}  // namespace linkerhand::hand::l6
