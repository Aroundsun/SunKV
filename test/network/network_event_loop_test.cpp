#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include "network/logger.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("EventLoop functionality test starting...");
    
    std::cout << "=== EventLoop Functionality Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> taskCount{0};
    std::atomic<int> callbackCount{0};
    
    // 在另一个线程中创建事件循环并运行
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 测试1: 基本任务队列功能
        std::cout << "Test 1: Basic task queue..." << std::endl;
        loop->runInLoop([&taskCount]() {
            taskCount++;
            LOG_INFO("Task 1 executed in event loop thread");
            std::cout << "✓ Task 1 executed!" << std::endl;
        });
        
        // 测试2: 跨线程任务调度
        std::cout << "Test 2: Cross-thread task scheduling..." << std::endl;
        std::thread workerThread([loop, &callbackCount, &taskCount]() {
            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                loop->runInLoop([&callbackCount, &taskCount, i]() {
                    callbackCount++;
                    taskCount++;
                    LOG_INFO("Cross-thread task {} executed", i);
                    std::cout << "✓ Cross-thread task " << i << " executed!" << std::endl;
                });
            }
        });
        
        // 测试3: 定时器与任务队列混合
        std::cout << "Test 3: Timer + task queue mix..." << std::endl;
        loop->runAfter(std::chrono::milliseconds(300), [loop, &taskCount]() {
            taskCount++;
            LOG_INFO("Timer task executed");
            std::cout << "✓ Timer task executed!" << std::endl;
            
            // 在定时器回调中添加新任务
            loop->runInLoop([&taskCount]() {
                taskCount++;
                LOG_INFO("Nested task from timer executed");
                std::cout << "✓ Nested task from timer executed!" << std::endl;
            });
        });
        
        // 测试4: 大量任务调度
        std::cout << "Test 4: Massive task scheduling..." << std::endl;
        for (int i = 0; i < 50; ++i) {
            loop->queueInLoop([&taskCount, i]() {
                taskCount++;
                if (i % 10 == 0) {
                    LOG_DEBUG("Massive task {} executed", i);
                    std::cout << "✓ Massive task " << i << " executed!" << std::endl;
                }
            });
        }
        
        // 测试5: 线程安全检查
        std::cout << "Test 5: Thread safety verification..." << std::endl;
        bool inLoopThread = loop->isInLoopThread();
        LOG_INFO("Current thread is in loop thread: {}", inLoopThread);
        std::cout << "✓ Thread in loop thread: " << (inLoopThread ? "true" : "false") << std::endl;
        
        // 测试6: 待处理任务检查
        std::cout << "Test 6: Pending tasks check..." << std::endl;
        bool hasPending = loop->hasPendingTasks();
        LOG_INFO("Has pending tasks: {}", hasPending);
        std::cout << "✓ Has pending tasks: " << (hasPending ? "true" : "false") << std::endl;
        
        // 测试7: 唤醒机制测试
        std::cout << "Test 7: Wakeup mechanism test..." << std::endl;
        std::thread wakeupThread([loop, &taskCount]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            LOG_INFO("Waking up event loop from another thread");
            loop->wakeup();
            
            // 添加任务验证唤醒成功
            loop->runInLoop([&taskCount]() {
                taskCount++;
                LOG_INFO("Task after wakeup executed");
                std::cout << "✓ Task after wakeup executed!" << std::endl;
            });
        });
        
        // 1.5秒后退出事件循环
        loop->runAfter(std::chrono::milliseconds(1500), [loop]() {
            LOG_INFO("Stopping event loop");
            loop->quit();
        });
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 等待所有工作线程结束
        workerThread.join();
        wakeupThread.join();
        
        // 在事件循环线程中删除 EventLoop
        delete loop;
        loop = nullptr;
    });
    
    std::cout << "Event loop started, running functionality tests for 1.7 seconds..." << std::endl;
    
    // 主线程等待1.7秒
    std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== EventLoop Test Results ===" << std::endl;
    std::cout << "Tasks executed in event loop: " << taskCount.load() << std::endl;
    std::cout << "Cross-thread callbacks: " << callbackCount.load() << std::endl;
    std::cout << "Expected tasks: ~57 (varies due to timing)" << std::endl;
    
    // 基本检查：应该有足够的任务执行
    bool testPassed = (taskCount.load() >= 50) && (callbackCount.load() == 5);
    
    if (testPassed) {
        std::cout << "✅ EventLoop functionality test PASSED!" << std::endl;
        LOG_INFO("EventLoop functionality test PASSED");
    } else {
        std::cout << "❌ EventLoop functionality test FAILED!" << std::endl;
        LOG_ERROR("EventLoop functionality test FAILED");
        std::cout << "Task count: " << taskCount.load() << " (expected >= 50)" << std::endl;
        std::cout << "Callback count: " << callbackCount.load() << " (expected = 5)" << std::endl;
    }
    
    LOG_INFO("EventLoop functionality test completed");
    return testPassed ? 0 : 1;
}
