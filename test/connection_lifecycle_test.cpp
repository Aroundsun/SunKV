#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Connection Lifecycle test starting...");
    
    std::cout << "=== Connection Lifecycle Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> connectionCount{0};
    std::atomic<int> maxConnections{0};
    std::vector<std::string> connectionLog;
    std::mutex logMutex;
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 在事件循环线程中创建 TcpServer
        auto server = std::make_unique<TcpServer>(loop, "LifecycleServer", "127.0.0.1", 8086);
        
        // 设置线程数量为2
        server->setThreadNum(2);
        
        // 设置连接回调
        server->setConnectionCallback([&connectionCount, &maxConnections, &connectionLog, &logMutex](const std::shared_ptr<TcpConnection>& conn) {
            std::lock_guard<std::mutex> lock(logMutex);
            
            if (conn->connected()) {
                connectionCount++;
                if (connectionCount.load() > maxConnections.load()) {
                    maxConnections = connectionCount.load();
                }
                
                std::string logEntry = "CONNECT: " + conn->peerAddress() + " (total: " + std::to_string(connectionCount.load()) + ")";
                connectionLog.push_back(logEntry);
                
                LOG_INFO("Connection established: {} (total: {})", conn->peerAddress(), connectionCount.load());
                std::cout << "✓ " << logEntry << std::endl;
                
                // 发送欢迎消息
                std::string welcome = "Welcome! You are connection #" + std::to_string(connectionCount.load()) + "\r\n";
                conn->send(welcome);
            } else {
                connectionCount--;
                
                std::string logEntry = "DISCONNECT: " + conn->peerAddress() + " (total: " + std::to_string(connectionCount.load()) + ")";
                connectionLog.push_back(logEntry);
                
                LOG_INFO("Connection closed: {} (total: {})", conn->peerAddress(), connectionCount.load());
                std::cout << "✗ " << logEntry << std::endl;
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            std::string message(static_cast<char*>(data), len);
            LOG_INFO("Message from {}: {}", conn->peerAddress(), message);
            
            // 回显消息
            conn->send(message);
        });
        
        std::cout << "Starting Lifecycle Test Server on 127.0.0.1:8086..." << std::endl;
        std::cout << "Thread pool size: 2" << std::endl;
        std::cout << "Test with: telnet 127.0.0.1 8086" << std::endl;
        std::cout << "Server will run for 8 seconds..." << std::endl;
        
        // 启动服务器
        server->start();
        
        // 8秒后停止服务器
        loop->runAfter(std::chrono::seconds(8), [loop]() {
            LOG_INFO("Stopping lifecycle server");
            loop->quit();
        });
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 在事件循环线程中先销毁 TcpServer，再销毁 EventLoop
        server.reset();
        delete loop;
        loop = nullptr;
    });
    
    std::cout << "Lifecycle Server running..." << std::endl;
    
    // 主线程等待10秒（确保服务器完全运行）
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== Connection Lifecycle Test Results ===" << std::endl;
    std::cout << "Maximum concurrent connections: " << maxConnections.load() << std::endl;
    std::cout << "Final connection count: " << connectionCount.load() << std::endl;
    std::cout << "Total connection events: " << connectionLog.size() << std::endl;
    
    std::cout << "\nConnection Event Log:" << std::endl;
    for (const auto& entry : connectionLog) {
        std::cout << "  " << entry << std::endl;
    }
    
    // 验证连接生命周期
    bool lifecycleCorrect = true;
    int currentCount = 0;
    for (const auto& entry : connectionLog) {
        if (entry.find("CONNECT:") == 0) {
            currentCount++;
        } else if (entry.find("DISCONNECT:") == 0) {
            currentCount--;
        }
        
        if (currentCount < 0 || currentCount > maxConnections.load()) {
            lifecycleCorrect = false;
            break;
        }
    }
    
    if (lifecycleCorrect && connectionCount.load() == 0) {
        std::cout << "✅ Connection Lifecycle test PASSED!" << std::endl;
        LOG_INFO("Connection Lifecycle test PASSED");
    } else {
        std::cout << "❌ Connection Lifecycle test FAILED!" << std::endl;
        LOG_ERROR("Connection Lifecycle test FAILED");
    }
    
    LOG_INFO("Connection Lifecycle test completed");
    return 0;
}
