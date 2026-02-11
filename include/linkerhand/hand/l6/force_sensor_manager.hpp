#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "linkerhand/can_dispatcher.hpp"
#include "linkerhand/iterable_queue.hpp"
#include "linkerhand/lifecycle.hpp"

namespace linkerhand::hand::l6 {

/// Finger indices for force sensor data.
enum class Finger : std::size_t {
  Thumb = 0,
  Index = 1,
  Middle = 2,
  Ring = 3,
  Pinky = 4,
};

inline constexpr std::size_t kFingerCount = 5;

/// CAN command prefix per finger.
inline constexpr std::array<std::uint8_t, kFingerCount> kFingerCommands = {
    0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
};

/// Get finger name as string_view (zero allocation).
inline constexpr std::string_view finger_name(Finger f) noexcept {
  constexpr std::string_view names[] = {"thumb", "index", "middle", "ring", "pinky"};
  const auto idx = static_cast<std::size_t>(f);
  return idx < kFingerCount ? names[idx] : "unknown";
}

struct ForceSensorData {
  std::array<std::uint8_t, 72> values{};
  double timestamp = 0.0;
};

/// All five fingers' data, stored as a fixed-size array (not a map).
struct AllFingersData {
  std::array<ForceSensorData, kFingerCount> fingers{};

  [[nodiscard]] const ForceSensorData& operator[](Finger f) const noexcept {
    assert(static_cast<std::size_t>(f) < kFingerCount);
    return fingers[static_cast<std::size_t>(f)];
  }
  [[nodiscard]] ForceSensorData& operator[](Finger f) noexcept {
    assert(static_cast<std::size_t>(f) < kFingerCount);
    return fingers[static_cast<std::size_t>(f)];
  }
};

class SingleForceSensorManager {
 public:
  SingleForceSensorManager(
      std::uint32_t arbitration_id,
      CANMessageDispatcher& dispatcher,
      std::uint8_t command_prefix,
      std::shared_ptr<linkerhand::Lifecycle> lifecycle);
  ~SingleForceSensorManager();

  SingleForceSensorManager(const SingleForceSensorManager&) = delete;
  SingleForceSensorManager& operator=(const SingleForceSensorManager&) = delete;

  [[nodiscard]] ForceSensorData get_data_blocking(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});
  [[nodiscard]] std::optional<ForceSensorData> get_latest_data() const;

  [[nodiscard]] IterableQueue<ForceSensorData> stream(
      std::chrono::milliseconds interval = std::chrono::milliseconds{100},
      std::size_t maxsize = 100);
  void stop_streaming();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle_;
};

class ForceSensorManager {
 public:
  ForceSensorManager(
      std::uint32_t arbitration_id,
      CANMessageDispatcher& dispatcher,
      std::shared_ptr<linkerhand::Lifecycle> lifecycle);
  ~ForceSensorManager();

  ForceSensorManager(const ForceSensorManager&) = delete;
  ForceSensorManager& operator=(const ForceSensorManager&) = delete;

  [[nodiscard]] AllFingersData get_data_blocking(
      std::chrono::milliseconds timeout = std::chrono::milliseconds{1000});

  [[nodiscard]] IterableQueue<AllFingersData> stream(
      std::chrono::milliseconds interval = std::chrono::milliseconds{100},
      std::size_t maxsize = 100);
  void stop_streaming();

  /// Get latest data per finger. Array indexed by Finger enum.
  [[nodiscard]] std::array<std::optional<ForceSensorData>, kFingerCount>
  get_latest_data() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle_;
};

}  // namespace linkerhand::hand::l6
