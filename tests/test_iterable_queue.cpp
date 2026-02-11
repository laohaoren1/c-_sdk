// tests/test_iterable_queue.cpp
// Unit tests for IterableQueue<T>

#include "linkerhand/iterable_queue.hpp"
#include "linkerhand/exceptions.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

#define ASSERT_EQ(a, b) \
  do { if ((a) != (b)) throw std::runtime_error( \
    std::string(#a " != " #b " (") + std::to_string(a) + " != " + std::to_string(b) + ")"); } while(0)

#define ASSERT_TRUE(x) \
  do { if (!(x)) throw std::runtime_error(#x " is false"); } while(0)

#define ASSERT_THROWS(expr, ExType) \
  do { bool caught = false; \
    try { expr; } catch (const ExType&) { caught = true; } \
    if (!caught) throw std::runtime_error(#expr " did not throw " #ExType); } while(0)

// ============================================================================

TEST(basic_put_get) {
  IterableQueue<int> q(10);
  q.put(42);
  q.put(43);
  ASSERT_EQ(q.get(), 42);
  ASSERT_EQ(q.get(), 43);
}

TEST(put_nowait_get_nowait) {
  IterableQueue<int> q(5);
  q.put_nowait(1);
  q.put_nowait(2);
  q.put_nowait(3);
  ASSERT_EQ(q.get_nowait(), 1);
  ASSERT_EQ(q.get_nowait(), 2);
  ASSERT_EQ(q.get_nowait(), 3);
}

TEST(get_nowait_empty_throws) {
  IterableQueue<int> q(5);
  ASSERT_THROWS(q.get_nowait(), QueueEmpty);
}

TEST(put_nowait_full_throws) {
  IterableQueue<int> q(2);
  q.put_nowait(1);
  q.put_nowait(2);
  ASSERT_THROWS(q.put_nowait(3), QueueFull);
}

TEST(unbounded_queue) {
  IterableQueue<int> q(0);  // maxsize=0 means unbounded
  for (int i = 0; i < 1000; ++i) {
    q.put_nowait(i);
  }
  for (int i = 0; i < 1000; ++i) {
    ASSERT_EQ(q.get_nowait(), i);
  }
}

TEST(close_stops_iteration) {
  IterableQueue<int> q(10);
  q.put(1);
  q.put(2);
  q.close();

  // Can still drain existing items
  ASSERT_EQ(q.get(), 1);
  ASSERT_EQ(q.get(), 2);

  // Then StopIteration
  ASSERT_THROWS(q.get(), StopIteration);
}

TEST(close_prevents_put) {
  IterableQueue<int> q(10);
  q.close();
  ASSERT_THROWS(q.put(1), StateError);
}

TEST(iterator_basic) {
  IterableQueue<int> q(10);
  q.put(10);
  q.put(20);
  q.put(30);
  q.close();

  std::vector<int> items;
  for (const auto& val : q) {
    items.push_back(val);
  }
  ASSERT_EQ(items.size(), 3u);
  ASSERT_EQ(items[0], 10);
  ASSERT_EQ(items[1], 20);
  ASSERT_EQ(items[2], 30);
}

TEST(blocking_get_with_timeout) {
  IterableQueue<int> q(10);
  // Should throw QueueEmpty on timeout
  ASSERT_THROWS(q.get(true, 0.01), QueueEmpty);
}

TEST(producer_consumer_threaded) {
  IterableQueue<int> q(5);
  constexpr int count = 100;

  std::thread producer([&] {
    for (int i = 0; i < count; ++i) {
      q.put(i);
    }
    q.close();
  });

  std::vector<int> consumed;
  for (const auto& val : q) {
    consumed.push_back(val);
  }
  producer.join();

  ASSERT_EQ(consumed.size(), static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) {
    ASSERT_EQ(consumed[i], i);
  }
}

TEST(empty_check) {
  IterableQueue<int> q(10);
  ASSERT_TRUE(q.empty());
  q.put(1);
  ASSERT_TRUE(!q.empty());
  q.get();
  ASSERT_TRUE(q.empty());
}

TEST(shared_state_across_copies) {
  IterableQueue<int> q1(10);
  IterableQueue<int> q2 = q1;  // shared_ptr copy
  q1.put(42);
  ASSERT_EQ(q2.get(), 42);
  q2.put(99);
  ASSERT_EQ(q1.get(), 99);
}

// ============================================================================

int main() {
  std::cout << "=== IterableQueue Tests ===\n";
  // Tests auto-register via static constructors above
  std::cout << "\nResults: " << tests_passed << " passed, "
            << tests_failed << " failed\n";
  return tests_failed > 0 ? 1 : 0;
}
