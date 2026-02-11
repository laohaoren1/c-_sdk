// tests/test_exceptions.cpp
// Unit tests for exception hierarchy

#include "linkerhand/exceptions.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
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

#define ASSERT_TRUE(x) \
  do { if (!(x)) throw std::runtime_error(#x " is false"); } while(0)

// ============================================================================

TEST(hierarchy_timeout) {
  try { throw TimeoutError("test"); }
  catch (const LinkerHandError&) { return; }
  throw std::runtime_error("TimeoutError not caught as LinkerHandError");
}

TEST(hierarchy_can) {
  try { throw CANError("test"); }
  catch (const LinkerHandError&) { return; }
  throw std::runtime_error("CANError not caught as LinkerHandError");
}

TEST(hierarchy_validation) {
  try { throw ValidationError("test"); }
  catch (const LinkerHandError&) { return; }
  throw std::runtime_error("ValidationError not caught as LinkerHandError");
}

TEST(hierarchy_state) {
  try { throw StateError("test"); }
  catch (const LinkerHandError&) { return; }
  throw std::runtime_error("StateError not caught as LinkerHandError");
}

TEST(all_catch_runtime_error) {
  // All should be catchable as std::runtime_error
  try { throw TimeoutError("t"); } catch (const std::runtime_error&) {}
  try { throw CANError("c"); } catch (const std::runtime_error&) {}
  try { throw ValidationError("v"); } catch (const std::runtime_error&) {}
  try { throw StateError("s"); } catch (const std::runtime_error&) {}
}

TEST(message_preserved) {
  TimeoutError e("hello world");
  ASSERT_TRUE(std::string(e.what()) == "hello world");
}

// ============================================================================

int main() {
  std::cout << "=== Exception Tests ===\n";
  std::cout << "\nResults: " << tests_passed << " passed, "
            << tests_failed << " failed\n";
  return tests_failed > 0 ? 1 : 0;
}
