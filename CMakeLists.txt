cmake_minimum_required(VERSION 3.16)

project(linkerhand_cpp_sdk LANGUAGES CXX)

file(GLOB_RECURSE LINKERHAND_CPP_SDK_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(linkerhand_cpp_sdk ${LINKERHAND_CPP_SDK_SOURCES})

target_compile_features(linkerhand_cpp_sdk PUBLIC cxx_std_20)
target_include_directories(linkerhand_cpp_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads REQUIRED)
target_link_libraries(linkerhand_cpp_sdk PRIVATE Threads::Threads)

# ============================================================================
# 测试程序
# ============================================================================
enable_testing()

set(TEST_SOURCES
  tests/test_iterable_queue.cpp
  tests/test_lifecycle.cpp
  tests/test_exceptions.cpp
  tests/test_can_message.cpp
  tests/test_force_sensor_types.cpp
)

foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE linkerhand_cpp_sdk Threads::Threads)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

