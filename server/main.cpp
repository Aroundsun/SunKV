#include <iostream>
#include <thread>
#include <chrono>
#include "network/logger.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("SunKV Server starting...");
    
    // 创建事件循环
    EventLoop loop;
    
    std::cout << "SunKV Server - High Performance KV Storage" << std::endl;
    std::cout << "Testing TimerQueue functionality..." << std::endl;
    
    // 测试一次性定时器
    loop.runAfter(std::chrono::milliseconds(1000), []() {
        LOG_INFO("One-shot timer fired after 1000ms");
        std::cout << "One-shot timer executed!" << std::endl;
    });
    
    // 测试重复定时器
    int counter = 0;
    loop.runEvery(std::chrono::milliseconds(500), [&counter]() {
        counter++;
        LOG_INFO("Repeated timer fired {} times", counter);
        std::cout << "Repeated timer executed, count: " << counter << std::endl;
        
        // 执行5次后停止
        if (counter >= 5) {
            // 这里需要通过其他方式停止，因为无法直接取消自己
        }
    });
    
    // 测试指定时间执行
    auto futureTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    loop.runAt(futureTime, []() {
        LOG_INFO("Timer fired at specific time");
        std::cout << "Timer at specific time executed!" << std::endl;
    });
    
    LOG_INFO("All timers scheduled, starting event loop for 3 seconds...");
    
    // 在另一个线程中运行事件循环
    std::thread loopThread([&loop]() {
        loop.loop();
    });
    
    // 主线程等待3秒后退出
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // 停止事件循环
    loop.quit();
    loopThread.join();
    
    LOG_INFO("SunKV Server test completed");
    std::cout << "TimerQueue test completed!" << std::endl;
    
    return 0;
}