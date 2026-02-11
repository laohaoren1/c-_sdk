// tests/test_force_sensor_types.cpp
// Unit tests for Finger enum, AllFingersData, and related types

#include "linkerhand/hand/l6/force_sensor_manager.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace linkerhand::hand::l6;

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
  do { if ((a) != (b)) throw std::runtime_error(#a " != " #b); } while(0)

#define ASSERT_TRUE(x) \
  do { if (!(x)) throw std::runtime_error(#x " is false"); } while(0)

// ============================================================================

TEST(finger_enum_values) {
  ASSERT_EQ(static_cast<std::size_t>(Finger::Thumb), 0u);
  ASSERT_EQ(static_cast<std::size_t>(Finger::Index), 1u);
  ASSERT_EQ(static_cast<std::size_t>(Finger::Middle), 2u);
  ASSERT_EQ(static_cast<std::size_t>(Finger::Ring), 3u);
  ASSERT_EQ(static_cast<std::size_t>(Finger::Pinky), 4u);
}

TEST(finger_count) {
  ASSERT_EQ(kFingerCount, 5u);
}

TEST(finger_commands) {
  ASSERT_EQ(kFingerCommands[0], 0xB1);
  ASSERT_EQ(kFingerCommands[1], 0xB2);
  ASSERT_EQ(kFingerCommands[2], 0xB3);
  ASSERT_EQ(kFingerCommands[3], 0xB4);
  ASSERT_EQ(kFingerCommands[4], 0xB5);
}

TEST(finger_name_function) {
  ASSERT_TRUE(finger_name(Finger::Thumb) == "thumb");
  ASSERT_TRUE(finger_name(Finger::Index) == "index");
  ASSERT_TRUE(finger_name(Finger::Middle) == "middle");
  ASSERT_TRUE(finger_name(Finger::Ring) == "ring");
  ASSERT_TRUE(finger_name(Finger::Pinky) == "pinky");
}

TEST(all_fingers_data_operator) {
  AllFingersData data{};
  data[Finger::Thumb].values[0] = 42;
  data[Finger::Pinky].values[71] = 99;

  ASSERT_EQ(data[Finger::Thumb].values[0], 42);
  ASSERT_EQ(data[Finger::Pinky].values[71], 99);
  // Also accessible via .fingers array
  ASSERT_EQ(data.fingers[0].values[0], 42);
  ASSERT_EQ(data.fingers[4].values[71], 99);
}

TEST(force_sensor_data_default) {
  ForceSensorData fsd{};
  ASSERT_EQ(fsd.timestamp, 0.0);
  for (auto v : fsd.values) {
    ASSERT_EQ(v, 0);
  }
}

// ============================================================================

int main() {
  std::cout << "=== ForceSensor Type Tests ===\n";
  std::cout << "\nResults: " << tests_passed << " passed, "
            << tests_failed << " failed\n";
  return tests_failed > 0 ? 1 : 0;
}
