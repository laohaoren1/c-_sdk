#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "linkerhand/exceptions.hpp"

namespace linkerhand {

enum class LifecycleState {
  Open,
  Closed,
};

class Lifecycle {
 public:
  using Callback = std::function<void()>;

  explicit Lifecycle(std::string owner_name = "L6") : owner_name_(std::move(owner_name)) {}

  Lifecycle(const Lifecycle&) = delete;
  Lifecycle& operator=(const Lifecycle&) = delete;

  [[nodiscard]] LifecycleState state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  [[nodiscard]] bool is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == LifecycleState::Open;
  }

  [[nodiscard]] bool is_closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == LifecycleState::Closed;
  }

  void ensure_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != LifecycleState::Open) {
      throw StateError(owner_name_ + " interface is closed. Create a new instance.");
    }
  }

  /// Idempotent close: sets state to Closed and notifies all subscribers.
  /// Returns true if this call performed the close, false if already closed.
  bool close() {
    std::vector<std::shared_ptr<Subscription>> subscriptions;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_ == LifecycleState::Closed) {
        return false;
      }
      state_ = LifecycleState::Closed;
      subscriptions.reserve(subscribers_.size());
      for (const auto& [id, sub] : subscribers_) {
        (void)id;
        subscriptions.push_back(sub);
      }
    }

    for (const auto& sub : subscriptions) {
      if (!sub || !sub->active.load()) continue;
      try { sub->cb(); } catch (...) {}
    }
    return true;
  }

  std::size_t subscribe(Callback callback) {
    if (!callback) {
      throw ValidationError("callback must be callable");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t id = next_subscription_id_++;
    auto sub = std::make_shared<Subscription>();
    sub->cb = std::move(callback);
    subscribers_.emplace(id, std::move(sub));
    return id;
  }

  void unsubscribe(std::size_t subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = subscribers_.find(subscription_id);
    if (it == subscribers_.end()) return;
    it->second->active.store(false);
    subscribers_.erase(it);
  }

 private:
  struct Subscription {
    std::atomic<bool> active{true};
    Callback cb;
  };

  const std::string owner_name_;

  mutable std::mutex mutex_;
  LifecycleState state_ = LifecycleState::Open;

  std::size_t next_subscription_id_ = 1;
  std::unordered_map<std::size_t, std::shared_ptr<Subscription>> subscribers_;
};

}  // namespace linkerhand
