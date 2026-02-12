// tests/test_hardware.cpp
// Hardware integration test — requires L6 right hand on can0

#include "linkerhand/linkerhand.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

using namespace linkerhand;
using namespace std::chrono_literals;

int main() {
  std::cout << "=== LinkerHand L6 硬件测试 ===\n\n";

  try {
    std::cout << "[1] 创建 L6 (right, can0)... ";
    hand::l6::L6 hand("right", "can0");
    std::cout << "✅\n";

    // --- 角度测试 ---
    std::cout << "\n--- 角度测试 ---\n";

    std::cout << "[2] 读取当前角度 (blocking, 500ms)... ";
    try {
      auto data = hand.angle.get_angles_blocking(500ms);
      std::cout << "✅ 角度: [";
      for (std::size_t i = 0; i < 6; ++i) {
        std::cout << data.angles[i];
        if (i < 5) std::cout << ", ";
      }
      std::cout << "] ts=" << data.timestamp << "\n";
    } catch (const TimeoutError& e) {
      std::cout << "⏰ 超时: " << e.what() << "\n";
    }

    std::cout << "[3] 设置角度: 全部张开 [255,255,255,255,255,255]... ";
    hand.angle.set_angles(std::array<int,6>{255, 255, 255, 255, 255, 255});
    std::cout << "✅ 已发送\n";
    std::this_thread::sleep_for(1500ms);

    std::cout << "[4] 读取角度确认... ";
    try {
      auto data = hand.angle.get_angles_blocking(500ms);
      std::cout << "✅ [";
      for (std::size_t i = 0; i < 6; ++i) {
        std::cout << data.angles[i];
        if (i < 5) std::cout << ", ";
      }
      std::cout << "]\n";
    } catch (const TimeoutError& e) {
      std::cout << "⏰ " << e.what() << "\n";
    }

    std::cout << "[5] 设置角度: 全部弯曲 [0,0,0,0,0,0]... ";
    hand.angle.set_angles(std::array<int,6>{0, 0, 0, 0, 0, 0});
    std::cout << "✅ 已发送\n";
    std::this_thread::sleep_for(1500ms);

    std::cout << "[6] 读取角度确认... ";
    try {
      auto data = hand.angle.get_angles_blocking(500ms);
      std::cout << "✅ [";
      for (std::size_t i = 0; i < 6; ++i) {
        std::cout << data.angles[i];
        if (i < 5) std::cout << ", ";
      }
      std::cout << "]\n";
    } catch (const TimeoutError& e) {
      std::cout << "⏰ " << e.what() << "\n";
    }

    std::cout << "[7] 恢复张开... ";
    hand.angle.set_angles(std::array<int,6>{255, 255, 255, 255, 255, 255});
    std::cout << "✅\n";
    std::this_thread::sleep_for(1000ms);

    // --- 流式测试 ---
    std::cout << "\n--- 角度流式测试 (采集5个样本) ---\n";
    std::cout << "[8] 启动 stream (100ms 间隔)... ";
    auto queue = hand.angle.stream(100ms, 50);
    std::cout << "✅\n";

    int count = 0;
    for (const auto& sample : queue) {
      std::cout << "  样本 " << count << ": [";
      for (std::size_t i = 0; i < 6; ++i) {
        std::cout << sample.angles[i];
        if (i < 5) std::cout << ", ";
      }
      std::cout << "]\n";
      if (++count >= 5) break;
    }
    hand.angle.stop_streaming();
    std::cout << "[9] stream 已停止 ✅\n";

    // --- 力传感器测试 ---
    std::cout << "\n--- 力传感器测试 ---\n";
    std::cout << "[10] 读取拇指力传感器 (1000ms)... ";
    try {
      auto latest = hand.force_sensor.get_latest_data();
      bool any_data = false;
      for (std::size_t i = 0; i < hand::l6::kFingerCount; ++i) {
        if (latest[i].has_value()) {
          any_data = true;
          std::cout << "\n  " << hand::l6::finger_name(static_cast<hand::l6::Finger>(i))
                    << ": 前6字节=[";
          for (int j = 0; j < 6; ++j) {
            std::cout << static_cast<int>(latest[i]->values[j]);
            if (j < 5) std::cout << ",";
          }
          std::cout << "...]";
        }
      }
      if (!any_data) std::cout << "无缓存数据（正常，未流式采集过）";
      std::cout << "\n";
    } catch (const std::exception& e) {
      std::cout << "❌ " << e.what() << "\n";
    }

    // --- 关闭 ---
    std::cout << "\n[11] 关闭手... ";
    hand.close();
    std::cout << "✅ is_closed=" << hand.is_closed() << "\n";

    std::cout << "[12] 再次关闭(幂等)... ";
    hand.close();
    std::cout << "✅\n";

    std::cout << "[13] close 后调用 set_angles 应抛异常... ";
    try {
      hand.angle.set_angles(std::array<int,6>{128,128,128,128,128,128});
      std::cout << "❌ 未抛异常!\n";
    } catch (const StateError& e) {
      std::cout << "✅ 捕获 StateError: " << e.what() << "\n";
    }

    std::cout << "\n=== 测试完成 ===\n";
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "❌ 致命错误: " << e.what() << "\n";
    return 1;
  }
}
