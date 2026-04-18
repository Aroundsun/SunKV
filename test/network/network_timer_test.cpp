#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "network/logger.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("TimerQueue test starting...");
    
    std::cout << "=== TimerQueue Test ===" << std::endl;
    
    std::atomic<int> oneShotCount{0};
    std::atomic<int> repeatCount{0};
    std::atomic<int> specificTimeCount{0};
    std::atomic<bool> shouldQuit{false};
    EventLoop* loopPtr = nullptr;
    
    // 在同一个线程中创建和运行 EventLoop
    std::thread loopThread([&]() {
        try {
            // 在目标线程中创建 EventLoop
            loopPtr = new EventLoop();
            LOG_INFO("EventLoop created in thread");
            
            // 测试1: 一次性定时器 - 500ms后执行
            loopPtr->runAfter(std::chrono::milliseconds(500), [&oneShotCount]() {
                oneShotCount++;
                LOG_INFO("One-shot timer fired, count: {}", oneShotCount.load());
                std::cout << "✓ One-shot timer executed!" << std::endl;
            });
            
            // 测试2: 重复定时器 - 每200ms执行一次
            loopPtr->runEvery(std::chrono::milliseconds(200), [&repeatCount]() {
                repeatCount++;
                LOG_INFO("Repeated timer fired, count: {}", repeatCount.load());
                std::cout << "✓ Repeated timer executed, count: " << repeatCount << std::endl;
            });
            
            // 测试3: 指定时间执行 - 800ms后执行
            auto futureTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
            loopPtr->runAt(futureTime, [&specificTimeCount]() {
                specificTimeCount++;
                LOG_INFO("Specific time timer fired, count: {}", specificTimeCount.load());
                std::cout << "✓ Timer at specific time executed!" << std::endl;
            });
            
            // 测试4: 取消定时器 - 300ms后取消一个600ms的定时器
            int64_t cancelTimerId = loopPtr->runAfter(std::chrono::milliseconds(600), []() {
                LOG_INFO("This timer should be cancelled");
                std::cout << "✗ This should not be printed!" << std::endl;
            });
            
            loopPtr->runAfter(std::chrono::milliseconds(300), [loopPtr, cancelTimerId]() {
                loopPtr->cancelTimer(cancelTimerId);
                LOG_INFO("Timer {} cancelled", cancelTimerId);
                std::cout << "✓ Timer cancelled successfully!" << std::endl;
            });
            
            std::cout << "All timers scheduled, running event loop for 1.5 seconds..." << std::endl;
            
            // 运行事件循环直到应该退出
            while (!shouldQuit) {
                loopPtr->loop();
            }
            
            LOG_INFO("Event loop thread finished normally");
        } catch (const std::exception& e) {
            LOG_ERROR("Event loop thread exception: {}", e.what());
            std::cout << "❌ Event loop exception: " << e.what() << std::endl;
        }
        delete loopPtr;
    });
    
    // 主线程等待1.5秒后设置退出标志
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    shouldQuit = true;
    if (loopPtr) {
        loopPtr->quit();
    }
    
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "One-shot timer count: " << oneShotCount.load() << " (expected: 1)" << std::endl;
    std::cout << "Repeat timer count: " << repeatCount.load() << " (expected: 7-8)" << std::endl;
    std::cout << "Specific time timer count: " << specificTimeCount.load() << " (expected: 1)" << std::endl;
    
    bool testPassed = (oneShotCount.load() == 1) && 
                     (repeatCount.load() >= 7) && 
                     (specificTimeCount.load() == 1);
    
    if (testPassed) {
        std::cout << "✅ TimerQueue test PASSED!" << std::endl;
        LOG_INFO("TimerQueue test PASSED");
    } else {
        std::cout << "❌ TimerQueue test FAILED!" << std::endl;
        LOG_ERROR("TimerQueue test FAILED");
    }
    
    LOG_INFO("TimerQueue test completed");
    return testPassed ? 0 : 1;
}
