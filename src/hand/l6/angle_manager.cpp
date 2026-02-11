#include "linkerhand/hand/l6/angle_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "linkerhand/exceptions.hpp"
#include "linkerhand/iterable_queue.hpp"
#include "../common.hpp"

namespace linkerhand::hand::l6 {
namespace {

constexpr std::uint8_t kControlCmd = 0x01;
constexpr std::size_t kAngleCount = 6;

}  // namespace

struct AngleManager::Impl {
  Impl(std::uint32_t arbitration_id,
       CANMessageDispatcher& dispatcher,
       std::shared_ptr<linkerhand::Lifecycle> lifecycle)
      : arbitration_id(arbitration_id),
        dispatcher(dispatcher),
        lifecycle(std::move(lifecycle)) {
    if (!this->lifecycle) {
      throw ValidationError("lifecycle must not be null");
    }
    subscription_id = dispatcher.subscribe(
        [this](const CanMessage& msg) { on_message(msg); });
    lifecycle_subscription_id = this->lifecycle->subscribe(
        [this] { cancel_all_waiters(); });
  }

  ~Impl() {
    try { stop_streaming(); } catch (...) {}
    try { lifecycle->unsubscribe(lifecycle_subscription_id); } catch (...) {}
    dispatcher.unsubscribe(subscription_id);
  }

  // ---- set_angles: unified via span ----

  void set_angles(std::span<const int> angles) {
    if (angles.size() != kAngleCount) {
      throw ValidationError("Expected 6 angles, got " +
                            std::to_string(angles.size()));
    }
    for (std::size_t i = 0; i < kAngleCount; ++i) {
      if (angles[i] < 0 || angles[i] > 255) {
        throw ValidationError("Angle " + std::to_string(i) + " value " +
                              std::to_string(angles[i]) +
                              " out of range [0, 255]");
      }
    }

    CanMessage msg{};
    msg.arbitration_id = arbitration_id;
    msg.is_extended_id = false;
    msg.dlc = 1 + kAngleCount;
    msg.data[0] = kControlCmd;
    for (std::size_t i = 0; i < kAngleCount; ++i) {
      msg.data[1 + i] = static_cast<std::uint8_t>(angles[i]);
    }
    dispatcher.send(msg);
  }

  // ---- get_angles_blocking: using std::promise/future ----

  AngleData get_angles_blocking(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
      throw ValidationError("timeout must be positive");
    }

    auto promise = std::make_shared<std::promise<AngleData>>();
    auto future = promise->get_future();

    {
      std::lock_guard<std::mutex> lock(waiters_mutex);
      waiters.push_back(promise);
    }

    // Send request if not streaming
    bool is_streaming = false;
    {
      std::lock_guard<std::mutex> lock(streaming_mutex);
      is_streaming = streaming_queue.has_value();
    }
    if (!is_streaming) {
      try {
        send_sense_request();
      } catch (...) {
        remove_waiter(promise);
        throw;
      }
    }

    const auto status = future.wait_for(timeout);
    if (status == std::future_status::ready) {
      return future.get();
    }

    // Timed out — clean up and check lifecycle
    remove_waiter(promise);
    lifecycle->ensure_open();
    throw TimeoutError("No angle data received within " +
                       std::to_string(timeout.count()) + "ms");
  }

  std::optional<AngleData> get_current_angles() const {
    std::lock_guard<std::mutex> lock(latest_mutex);
    return latest_data;
  }

  // ---- streaming ----

  IterableQueue<AngleData> stream(std::chrono::milliseconds interval,
                                  std::size_t maxsize) {
    if (interval.count() <= 0) {
      throw ValidationError("interval must be positive");
    }
    if (maxsize == 0) {
      throw ValidationError("maxsize must be positive");
    }
    if (!dispatcher.is_running()) {
      throw StateError("CAN dispatcher is stopped");
    }

    IterableQueue<AngleData> queue(maxsize);
    {
      std::lock_guard<std::mutex> lock(streaming_mutex);
      if (streaming_queue.has_value() || streaming_thread.joinable()) {
        throw StateError("Streaming is already active. Call stop_streaming() first.");
      }
      streaming_queue = queue;
      streaming_running.store(true);
      try {
        streaming_thread = std::thread([this, interval, queue] {
          try {
            streaming_loop(interval);
          } catch (...) {}
          queue.close();
          streaming_running.store(false);
        });
      } catch (...) {
        streaming_running.store(false);
        streaming_queue.reset();
        throw;
      }
    }
    return queue;
  }

  void stop_streaming() {
    std::lock_guard<std::mutex> lock(streaming_mutex);
    if (!streaming_queue.has_value()) {
      if (streaming_thread.joinable()) {
        streaming_running.store(false);
        streaming_thread.join();
      }
      return;
    }
    streaming_running.store(false);
    streaming_queue->close();
    streaming_queue.reset();
    if (streaming_thread.joinable()) {
      streaming_thread.join();
    }
  }

 private:
  void send_sense_request() {
    CanMessage msg{};
    msg.arbitration_id = arbitration_id;
    msg.is_extended_id = false;
    msg.dlc = 1;
    msg.data[0] = kControlCmd;
    dispatcher.send(msg);
  }

  void streaming_loop(std::chrono::milliseconds interval) {
    while (streaming_running.load()) {
      send_sense_request();
      std::this_thread::sleep_for(interval);
    }
  }

  void on_message(const CanMessage& msg) {
    if (msg.arbitration_id != arbitration_id) return;
    if (msg.dlc < 2 || msg.data[0] != kControlCmd) return;
    if (msg.dlc - 1 != kAngleCount) return;

    AngleData data{};
    for (std::size_t i = 0; i < kAngleCount; ++i) {
      data.angles[i] = static_cast<int>(msg.data[1 + i]);
    }
    data.timestamp = detail::now_unix_seconds();

    dispatch_data(data);
  }

  void dispatch_data(const AngleData& data) {
    // Update cache
    {
      std::lock_guard<std::mutex> lock(latest_mutex);
      latest_data = data;
    }

    // Fulfill all waiting promises
    {
      std::lock_guard<std::mutex> lock(waiters_mutex);
      for (auto& p : waiters) {
        try { p->set_value(data); } catch (...) {}
      }
      waiters.clear();
    }

    // Push to streaming queue (drop oldest if full)
    std::optional<IterableQueue<AngleData>> q;
    {
      std::lock_guard<std::mutex> lock(streaming_mutex);
      q = streaming_queue;
    }
    if (!q.has_value()) return;

    try {
      q->put_nowait(data);
    } catch (const StateError&) {
    } catch (const QueueFull&) {
      try { q->get_nowait(); q->put_nowait(data); }
      catch (const QueueEmpty&) {}
    }
  }

  void cancel_all_waiters() {
    std::lock_guard<std::mutex> lock(waiters_mutex);
    for (auto& p : waiters) {
      try {
        p->set_exception(std::make_exception_ptr(
            StateError("Interface closed")));
      } catch (...) {}
    }
    waiters.clear();
  }

  void remove_waiter(const std::shared_ptr<std::promise<AngleData>>& waiter) {
    std::lock_guard<std::mutex> lock(waiters_mutex);
    waiters.erase(
        std::remove(waiters.begin(), waiters.end(), waiter),
        waiters.end());
  }

  std::uint32_t arbitration_id;
  CANMessageDispatcher& dispatcher;
  std::size_t subscription_id = 0;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle;
  std::size_t lifecycle_subscription_id = 0;

  mutable std::mutex latest_mutex;
  std::optional<AngleData> latest_data;

  mutable std::mutex waiters_mutex;
  std::vector<std::shared_ptr<std::promise<AngleData>>> waiters;

  mutable std::mutex streaming_mutex;
  std::optional<IterableQueue<AngleData>> streaming_queue;
  std::atomic<bool> streaming_running{false};
  std::thread streaming_thread;
};

// ---- Public API ----

AngleManager::AngleManager(
    std::uint32_t arbitration_id,
    CANMessageDispatcher& dispatcher,
    std::shared_ptr<linkerhand::Lifecycle> lifecycle)
    : impl_(std::make_unique<Impl>(arbitration_id, dispatcher, lifecycle)),
      lifecycle_(std::move(lifecycle)) {}

AngleManager::~AngleManager() = default;

void AngleManager::set_angles(std::span<const int> angles) {
  lifecycle_->ensure_open();
  impl_->set_angles(angles);
}

AngleData AngleManager::get_angles_blocking(std::chrono::milliseconds timeout) {
  lifecycle_->ensure_open();
  return impl_->get_angles_blocking(timeout);
}

std::optional<AngleData> AngleManager::get_current_angles() const {
  lifecycle_->ensure_open();
  return impl_->get_current_angles();
}

IterableQueue<AngleData> AngleManager::stream(
    std::chrono::milliseconds interval, std::size_t maxsize) {
  lifecycle_->ensure_open();
  return impl_->stream(interval, maxsize);
}

void AngleManager::stop_streaming() {
  impl_->stop_streaming();
}

}  // namespace linkerhand::hand::l6
