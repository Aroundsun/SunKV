#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "network/logger.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Network layer test starting...");
    
    std::cout << "=== Network Layer Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> eventCount{0};
    
    // 在另一个线程中创建事件循环并运行
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 测试1: 创建 socket 并测试 Channel
        std::cout << "Test 1: Socket and Channel test..." << std::endl;
        
        // 创建一个 TCP socket
        int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) {
            LOG_ERROR("Failed to create socket");
            return;
        }
        
        LOG_INFO("Socket {} created", sockfd);
        std::cout << "✓ Socket created: " << sockfd << std::endl;
        
        // 创建 Channel
        // 注意：这里我们不能直接创建 Channel，因为需要 EventLoop 的内部接口
        // 但我们可以测试 EventLoop 的基本功能
        
        // 测试2: EventLoop 基本功能
        std::cout << "Test 2: EventLoop basic functionality..." << std::endl;
        
        loop->runInLoop([&eventCount]() {
            eventCount++;
            LOG_INFO("EventLoop task executed");
            std::cout << "✓ EventLoop task executed!" << std::endl;
        });
        
        // 测试3: 定时器集成测试
        std::cout << "Test 3: Timer integration test..." << std::endl;
        
        loop->runAfter(std::chrono::milliseconds(200), [&eventCount, sockfd]() {
            eventCount++;
            LOG_INFO("Timer with network integration executed");
            std::cout << "✓ Timer integration executed!" << std::endl;
            
            // 测试 socket 操作
            int result = close(sockfd);
            if (result == 0) {
                LOG_INFO("Socket {} closed successfully", sockfd);
                std::cout << "✓ Socket closed successfully!" << std::endl;
            } else {
                LOG_ERROR("Failed to close socket {}", sockfd);
            }
        });
        
        // 测试4: 多任务并发测试
        std::cout << "Test 4: Concurrent task test..." << std::endl;
        
        for (int i = 0; i < 10; ++i) {
            loop->queueInLoop([&eventCount, i]() {
                eventCount++;
                if (i % 3 == 0) {
                    LOG_DEBUG("Concurrent task {} executed", i);
                    std::cout << "✓ Concurrent task " << i << " executed!" << std::endl;
                }
            });
        }
        
        // 测试5: 线程安全验证
        std::cout << "Test 5: Thread safety verification..." << std::endl;
        
        bool inLoopThread = loop->isInLoopThread();
        LOG_INFO("Current thread in loop thread: {}", inLoopThread);
        std::cout << "✓ Thread safety check: " << (inLoopThread ? "PASS" : "FAIL") << std::endl;
        
        // 1秒后退出事件循环
        loop->runAfter(std::chrono::milliseconds(1000), [loop]() {
            LOG_INFO("Stopping event loop");
            loop->quit();
        });
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 在事件循环线程中删除 EventLoop
        delete loop;
        loop = nullptr;
    });
    
    std::cout << "Event loop started, running network tests for 1.2 seconds..." << std::endl;
    
    // 主线程等待1.2秒
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== Network Layer Test Results ===" << std::endl;
    std::cout << "Events processed: " << eventCount.load() << std::endl;
    std::cout << "Expected events: ~12-15" << std::endl;
    
    bool testPassed = eventCount.load() >= 10;
    
    if (testPassed) {
        std::cout << "✅ Network layer test PASSED!" << std::endl;
        LOG_INFO("Network layer test PASSED");
    } else {
        std::cout << "❌ Network layer test FAILED!" << std::endl;
        LOG_ERROR("Network layer test FAILED");
    }
    
    LOG_INFO("Network layer test completed");
    return testPassed ? 0 : 1;
}
