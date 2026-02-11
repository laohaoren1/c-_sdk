// tests/test_can_message.cpp
// Unit tests for CanMessage struct

#include "linkerhand/can_dispatcher.hpp"

#include <cassert>
#include <iostream>
#include <string>

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
    std::string(#a " != " #b)); } while(0)

#define ASSERT_TRUE(x) \
  do { if (!(x)) throw std::runtime_error(#x " is false"); } while(0)

// ============================================================================

TEST(default_construction) {
  CanMessage msg{};
  ASSERT_EQ(msg.arbitration_id, 0u);
  ASSERT_TRUE(!msg.is_extended_id);
  ASSERT_EQ(msg.dlc, 0u);
}

TEST(data_bytes_empty) {
  CanMessage msg{};
  msg.dlc = 0;
  auto bytes = msg.data_bytes();
  ASSERT_TRUE(bytes.empty());
}

TEST(data_bytes_partial) {
  CanMessage msg{};
  msg.dlc = 3;
  msg.data[0] = 0xAA;
  msg.data[1] = 0xBB;
  msg.data[2] = 0xCC;
  msg.data[3] = 0xFF;  // should NOT be included

  auto bytes = msg.data_bytes();
  ASSERT_EQ(bytes.size(), 3u);
  ASSERT_EQ(bytes[0], 0xAA);
  ASSERT_EQ(bytes[1], 0xBB);
  ASSERT_EQ(bytes[2], 0xCC);
}

TEST(data_bytes_full) {
  CanMessage msg{};
  msg.dlc = 8;
  for (int i = 0; i < 8; ++i) {
    msg.data[i] = static_cast<std::uint8_t>(i + 1);
  }
  auto bytes = msg.data_bytes();
  ASSERT_EQ(bytes.size(), 8u);
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(bytes[i], static_cast<std::uint8_t>(i + 1));
  }
}

TEST(dispatcher_config_defaults) {
  CANDispatcherConfig config{};
  ASSERT_EQ(config.poll_timeout_ms, 10);
}

// ============================================================================

int main() {
  std::cout << "=== CanMessage Tests ===\n";
  std::cout << "\nResults: " << tests_passed << " passed, "
            << tests_failed << " failed\n";
  return tests_failed > 0 ? 1 : 0;
}
