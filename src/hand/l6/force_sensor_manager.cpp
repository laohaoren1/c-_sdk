#include "linkerhand/hand/l6/force_sensor_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "linkerhand/exceptions.hpp"
#include "linkerhand/iterable_queue.hpp"

#include "../common.hpp"

namespace linkerhand::hand::l6 {
namespace {

constexpr std::size_t kFrameCount = 12;
constexpr std::size_t kBytesPerFrame = 6;

struct FrameBatch {
  std::array<std::optional<std::array<std::uint8_t, kBytesPerFrame>>, kFrameCount> frames{};
  std::size_t count = 0;

  [[nodiscard]] FrameBatch add_frame(
      std::size_t frame_id,
      const std::array<std::uint8_t, kBytesPerFrame>& data) const {
    FrameBatch next = *this;
    if (!next.frames[frame_id].has_value()) {
      next.count += 1;
    }
    next.frames[frame_id] = data;
    return next;
  }

  [[nodiscard]] bool is_complete() const noexcept { return count == kFrameCount; }

  [[nodiscard]] ForceSensorData assemble() const {
    ForceSensorData out{};
    std::size_t offset = 0;
    for (std::size_t i = 0; i < kFrameCount; ++i) {
      if (!frames[i].has_value()) {
        throw StateError("Incomplete frame batch");
      }
      for (std::size_t j = 0; j < kBytesPerFrame; ++j) {
        out.values[offset++] = (*frames[i])[j];
      }
    }
    out.timestamp = detail::now_unix_seconds();
    return out;
  }
};

}  // namespace

// ============================================================================
// SingleForceSensorManager::Impl
// ============================================================================

struct SingleForceSensorManager::Impl {
  Impl(std::uint32_t arbitration_id,
       CANMessageDispatcher& dispatcher,
       std::uint8_t command_prefix,
       std::shared_ptr<linkerhand::Lifecycle> lifecycle)
      : arbitration_id(arbitration_id),
        dispatcher(dispatcher),
        command_prefix(command_prefix),
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

  ForceSensorData get_data_blocking(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
      throw ValidationError("timeout must be positive");
    }

    auto promise = std::make_shared<std::promise<ForceSensorData>>();
    auto future = promise->get_future();

    {
      std::lock_guard<std::mutex> lock(waiters_mutex);
      waiters.push_back(promise);
    }

    try {
      send_request();
    } catch (...) {
      remove_waiter(promise);
      throw;
    }

    const auto status = future.wait_for(timeout);
    if (status == std::future_status::ready) {
      return future.get();
    }

    remove_waiter(promise);
    lifecycle->ensure_open();
    throw TimeoutError("No force data received within " +
                       std::to_string(timeout.count()) + "ms");
  }

  std::optional<ForceSensorData> get_latest_data() const {
    std::lock_guard<std::mutex> lock(latest_mutex);
    return latest_data;
  }

  IterableQueue<ForceSensorData> stream(std::chrono::milliseconds interval,
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

    IterableQueue<ForceSensorData> queue(maxsize);
    {
      std::lock_guard<std::mutex> lock(streaming_mutex);
      if (streaming_queue.has_value() || streaming_thread.joinable()) {
        throw StateError("Streaming is already active. Call stop_streaming() first.");
      }
      streaming_queue = queue;
      streaming_running.store(true);
      try {
        streaming_thread = std::thread([this, interval, queue] {
          try { streaming_loop(interval); } catch (...) {}
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
  void send_request() {
    CanMessage msg{};
    msg.arbitration_id = arbitration_id;
    msg.is_extended_id = false;
    msg.dlc = 2;
    msg.data[0] = command_prefix;
    msg.data[1] = 0xC6;
    dispatcher.send(msg);
  }

  void streaming_loop(std::chrono::milliseconds interval) {
    while (streaming_running.load()) {
      send_request();
      std::this_thread::sleep_for(interval);
    }
  }

  void on_message(const CanMessage& msg) {
    if (msg.arbitration_id != arbitration_id) return;
    if (msg.dlc < 8 || msg.data[0] != command_prefix) return;

    const std::size_t frame_idx = static_cast<std::size_t>(msg.data[1] >> 4);
    if (frame_idx >= kFrameCount) return;

    std::array<std::uint8_t, kBytesPerFrame> frame_data{};
    for (std::size_t i = 0; i < kBytesPerFrame; ++i) {
      frame_data[i] = msg.data[2 + i];
    }

    if (!frame_batch.has_value()) {
      frame_batch = FrameBatch{};
    }
    frame_batch = frame_batch->add_frame(frame_idx, frame_data);

    if (!frame_batch->is_complete()) return;

    ForceSensorData complete_data = frame_batch->assemble();
    frame_batch.reset();
    dispatch_data(complete_data);
  }

  void dispatch_data(const ForceSensorData& data) {
    {
      std::lock_guard<std::mutex> lock(latest_mutex);
      latest_data = data;
    }

    {
      std::lock_guard<std::mutex> lock(waiters_mutex);
      for (auto& p : waiters) {
        try { p->set_value(data); } catch (...) {}
      }
      waiters.clear();
    }

    std::optional<IterableQueue<ForceSensorData>> q;
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
        p->set_exception(std::make_exception_ptr(StateError("Interface closed")));
      } catch (...) {}
    }
    waiters.clear();
  }

  void remove_waiter(const std::shared_ptr<std::promise<ForceSensorData>>& waiter) {
    std::lock_guard<std::mutex> lock(waiters_mutex);
    waiters.erase(std::remove(waiters.begin(), waiters.end(), waiter), waiters.end());
  }

  std::uint32_t arbitration_id;
  CANMessageDispatcher& dispatcher;
  std::uint8_t command_prefix;
  std::size_t subscription_id = 0;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle;
  std::size_t lifecycle_subscription_id = 0;

  std::optional<FrameBatch> frame_batch;

  mutable std::mutex latest_mutex;
  std::optional<ForceSensorData> latest_data;

  mutable std::mutex waiters_mutex;
  std::vector<std::shared_ptr<std::promise<ForceSensorData>>> waiters;

  mutable std::mutex streaming_mutex;
  std::optional<IterableQueue<ForceSensorData>> streaming_queue;
  std::atomic<bool> streaming_running{false};
  std::thread streaming_thread;
};

// ---- SingleForceSensorManager public API ----

SingleForceSensorManager::SingleForceSensorManager(
    std::uint32_t arbitration_id,
    CANMessageDispatcher& dispatcher,
    std::uint8_t command_prefix,
    std::shared_ptr<linkerhand::Lifecycle> lifecycle)
    : impl_(std::make_unique<Impl>(arbitration_id, dispatcher, command_prefix, lifecycle)),
      lifecycle_(std::move(lifecycle)) {}

SingleForceSensorManager::~SingleForceSensorManager() = default;

ForceSensorData SingleForceSensorManager::get_data_blocking(std::chrono::milliseconds timeout) {
  lifecycle_->ensure_open();
  return impl_->get_data_blocking(timeout);
}

std::optional<ForceSensorData> SingleForceSensorManager::get_latest_data() const {
  lifecycle_->ensure_open();
  return impl_->get_latest_data();
}

IterableQueue<ForceSensorData> SingleForceSensorManager::stream(
    std::chrono::milliseconds interval, std::size_t maxsize) {
  lifecycle_->ensure_open();
  return impl_->stream(interval, maxsize);
}

void SingleForceSensorManager::stop_streaming() {
  impl_->stop_streaming();
}

// ============================================================================
// ForceSensorManager::Impl — aggregation layer using array instead of map
// ============================================================================

struct ForceSensorManager::Impl {
  Impl(std::uint32_t arbitration_id,
       CANMessageDispatcher& dispatcher,
       std::shared_ptr<linkerhand::Lifecycle> lifecycle)
      : arbitration_id(arbitration_id),
        dispatcher(dispatcher),
        lifecycle(std::move(lifecycle)) {
    if (!this->lifecycle) {
      throw ValidationError("lifecycle must not be null");
    }
    for (std::size_t i = 0; i < kFingerCount; ++i) {
      fingers[i] = std::make_unique<SingleForceSensorManager>(
          arbitration_id, dispatcher, kFingerCommands[i], this->lifecycle);
    }
  }

  ~Impl() {
    try { stop_streaming(); } catch (...) {}
  }

  AllFingersData get_data_blocking(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
      throw ValidationError("timeout must be positive");
    }
    AllFingersData out{};
    for (std::size_t i = 0; i < kFingerCount; ++i) {
      out.fingers[i] = fingers[i]->get_data_blocking(timeout);
    }
    return out;
  }

  IterableQueue<AllFingersData> stream(std::chrono::milliseconds interval,
                                       std::size_t maxsize) {
    if (interval.count() <= 0) {
      throw ValidationError("interval must be positive");
    }
    if (maxsize == 0) {
      throw ValidationError("maxsize must be positive");
    }

    IterableQueue<AllFingersData> queue(maxsize);
    {
      std::lock_guard<std::mutex> lock(streaming_mutex);
      if (streaming_queue.has_value() || aggregation_thread.joinable()) {
        throw StateError("Streaming is already active. Call stop_streaming() first.");
      }
      streaming_queue = queue;

      // Start per-finger streams
      std::array<IterableQueue<ForceSensorData>, kFingerCount> finger_queues{
          IterableQueue<ForceSensorData>(0),
          IterableQueue<ForceSensorData>(0),
          IterableQueue<ForceSensorData>(0),
          IterableQueue<ForceSensorData>(0),
          IterableQueue<ForceSensorData>(0),
      };
      try {
        for (std::size_t i = 0; i < kFingerCount; ++i) {
          finger_queues[i] = fingers[i]->stream(interval, maxsize);
        }
        aggregation_running.store(true);
        aggregation_thread = std::thread(
            [this, finger_queues, queue] {
              try { aggregation_loop(finger_queues, queue); } catch (...) {}
            });
      } catch (...) {
        for (auto& f : fingers) {
          try { f->stop_streaming(); } catch (...) {}
        }
        aggregation_running.store(false);
        streaming_queue.reset();
        throw;
      }
    }
    return queue;
  }

  void stop_streaming() {
    std::lock_guard<std::mutex> lock(streaming_mutex);
    if (!streaming_queue.has_value()) {
      if (aggregation_thread.joinable()) {
        aggregation_running.store(false);
        aggregation_thread.join();
      }
      for (auto& t : reader_threads) {
        if (t.joinable()) t.join();
      }
      reader_threads.clear();
      return;
    }

    for (auto& f : fingers) {
      f->stop_streaming();
    }

    aggregation_running.store(false);
    streaming_queue->close();
    streaming_queue.reset();

    if (aggregation_thread.joinable()) {
      aggregation_thread.join();
    }
    for (auto& t : reader_threads) {
      if (t.joinable()) t.join();
    }
    reader_threads.clear();
  }

  std::array<std::optional<ForceSensorData>, kFingerCount> get_latest_data() const {
    std::array<std::optional<ForceSensorData>, kFingerCount> out{};
    for (std::size_t i = 0; i < kFingerCount; ++i) {
      out[i] = fingers[i]->get_latest_data();
    }
    return out;
  }

 private:
  void aggregation_loop(
      const std::array<IterableQueue<ForceSensorData>, kFingerCount>& finger_queues,
      IterableQueue<AllFingersData> output_queue) {
    // Intermediate queue: (finger_index, data)
    auto intermediate =
        std::make_shared<IterableQueue<std::pair<std::size_t, ForceSensorData>>>();

    reader_threads.clear();
    for (std::size_t i = 0; i < kFingerCount; ++i) {
      auto fq = finger_queues[i];
      reader_threads.emplace_back([i, fq, intermediate] {
        try {
          for (const auto& data : fq) {
            intermediate->put({i, data});
          }
        } catch (...) {}
      });
    }

    std::array<std::optional<ForceSensorData>, kFingerCount> latest{};
    std::size_t collected = 0;

    while (aggregation_running.load()) {
      std::pair<std::size_t, ForceSensorData> item;
      try {
        item = intermediate->get(/*block=*/true, /*timeout_s=*/1.0);
      } catch (const QueueEmpty&) {
        continue;
      } catch (const StopIteration&) {
        break;
      }

      if (!latest[item.first].has_value()) {
        collected++;
      }
      latest[item.first] = item.second;

      if (collected != kFingerCount) continue;

      AllFingersData snapshot{};
      for (std::size_t i = 0; i < kFingerCount; ++i) {
        snapshot.fingers[i] = *latest[i];
      }

      try {
        output_queue.put_nowait(snapshot);
      } catch (const StateError&) {
        break;
      } catch (const QueueFull&) {
        try { output_queue.get_nowait(); output_queue.put_nowait(snapshot); }
        catch (const QueueEmpty&) {}
        catch (const StateError&) { break; }
      }

      latest = {};
      collected = 0;
    }

    intermediate->close();
    output_queue.close();
  }

  std::uint32_t arbitration_id;
  CANMessageDispatcher& dispatcher;
  std::shared_ptr<linkerhand::Lifecycle> lifecycle;

  std::array<std::unique_ptr<SingleForceSensorManager>, kFingerCount> fingers;

  mutable std::mutex streaming_mutex;
  std::optional<IterableQueue<AllFingersData>> streaming_queue;
  std::atomic<bool> aggregation_running{false};
  std::thread aggregation_thread;
  std::vector<std::thread> reader_threads;
};

// ---- ForceSensorManager public API ----

ForceSensorManager::ForceSensorManager(
    std::uint32_t arbitration_id,
    CANMessageDispatcher& dispatcher,
    std::shared_ptr<linkerhand::Lifecycle> lifecycle)
    : impl_(std::make_unique<Impl>(arbitration_id, dispatcher, lifecycle)),
      lifecycle_(std::move(lifecycle)) {}

ForceSensorManager::~ForceSensorManager() = default;

AllFingersData ForceSensorManager::get_data_blocking(std::chrono::milliseconds timeout) {
  lifecycle_->ensure_open();
  return impl_->get_data_blocking(timeout);
}

IterableQueue<AllFingersData> ForceSensorManager::stream(
    std::chrono::milliseconds interval, std::size_t maxsize) {
  lifecycle_->ensure_open();
  return impl_->stream(interval, maxsize);
}

void ForceSensorManager::stop_streaming() {
  impl_->stop_streaming();
}

std::array<std::optional<ForceSensorData>, kFingerCount>
ForceSensorManager::get_latest_data() const {
  lifecycle_->ensure_open();
  return impl_->get_latest_data();
}

}  // namespace linkerhand::hand::l6
