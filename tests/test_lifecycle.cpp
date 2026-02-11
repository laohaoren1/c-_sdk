// tests/test_lifecycle.cpp
// Unit tests for Lifecycle

#include "linkerhand/lifecycle.hpp"
#include "linkerhand/exceptions.hpp"

#include <atomic>
#include <cassert>
#include <iostream>
#include <string>
#include <thread>

using namespace linkerhand;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
  static void test_##name(); \
  struct Register_##name { \
    Register_##name() { \
      std::cout << "  " #name "... "; \
      try { test_##name(); tests_passed++; std::cout << "✅\n"; } \
      catch (const std::exception& e) { tests_failed++; std::cout << "❌ " << e.what() << "\n"; } \
      catch (...) { tests_failed++; std::cout << "❌ unknown\n"; } \
    } \
  } register_##name; \
  static void test_##name()

#define ASSERT_TRUE(x) \
  do { if (!(x)) throw std::runtime_error(#x " is false"); } while(0)

#define ASSERT_THROWS(expr, ExType) \
  do { bool caught = false; \
    try { expr; } catch (const ExType&) { caught = true; } \
    if (!caught) throw std::runtime_error(#expr " did not throw " #ExType); } while(0)

// ============================================================================

TEST(initial_state_is_open) {
  Lifecycle lc("Test");
  ASSERT_TRUE(lc.is_open());
  ASSERT_TRUE(!lc.is_closed());
  ASSERT_TRUE(lc.state() == LifecycleState::Open);
}

TEST(close_changes_state) {
  Lifecycle lc("Test");
  bool result = lc.close();
  ASSERT_TRUE(result);
  ASSERT_TRUE(lc.is_closed());
  ASSERT_TRUE(!lc.is_open());
  ASSERT_TRUE(lc.state() == LifecycleState::Closed);
}

TEST(close_is_idempotent) {
  Lifecycle lc("Test");
  ASSERT_TRUE(lc.close());
  ASSERT_TRUE(!lc.close());  // second call returns false
  ASSERT_TRUE(!lc.close());  // third call still false
}

TEST(ensure_open_throws_after_close) {
  Lifecycle lc("Test");
  lc.ensure_open();  // should not throw
  lc.close();
  ASSERT_THROWS(lc.ensure_open(), StateError);
}

TEST(subscribe_callback_on_close) {
  Lifecycle lc("Test");
  std::atomic<int> called{0};
  lc.subscribe([&] { called++; });
  lc.close();
  ASSERT_TRUE(called.load() == 1);
}

TEST(multiple_subscribers) {
  Lifecycle lc("Test");
  std::atomic<int> count{0};
  lc.subscribe([&] { count++; });
  lc.subscribe([&] { count++; });
  lc.subscribe([&] { count++; });
  lc.close();
  ASSERT_TRUE(count.load() == 3);
}

TEST(unsubscribe_prevents_callback) {
  Lifecycle lc("Test");
  std::atomic<int> count{0};
  auto id = lc.subscribe([&] { count++; });
  lc.unsubscribe(id);
  lc.close();
  ASSERT_TRUE(count.load() == 0);
}

TEST(unsubscribe_nonexistent_is_safe) {
  Lifecycle lc("Test");
  lc.unsubscribe(9999);  // should not throw
}

TEST(subscribe_null_callback_throws) {
  Lifecycle lc("Test");
  ASSERT_THROWS(lc.subscribe(nullptr), ValidationError);
}

TEST(callback_not_called_on_second_close) {
  Lifecycle lc("Test");
  std::atomic<int> count{0};
  lc.subscribe([&] { count++; });
  lc.close();
  lc.close();
  ASSERT_TRUE(count.load() == 1);  // only called once
}

TEST(close_from_multiple_threads) {
  Lifecycle lc("Test");
  std::atomic<int> close_count{0};
  constexpr int threads = 10;
  std::vector<std::thread> workers;
  for (int i = 0; i < threads; ++i) {
    workers.emplace_back([&] {
      if (lc.close()) {
        close_count++;
      }
    });
  }
  for (auto& t : workers) t.join();
  ASSERT_TRUE(close_count.load() == 1);  // exactly one succeeds
}

// ============================================================================

int main() {
  std::cout << "=== Lifecycle Tests ===\n";
  std::cout << "\nResults: " << tests_passed << " passed, "
            << tests_failed << " failed\n";
  return tests_failed > 0 ? 1 : 0;
}
