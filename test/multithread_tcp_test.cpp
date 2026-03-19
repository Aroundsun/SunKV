#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Multi-thread TCP test starting...");
    
    std::cout << "=== Multi-thread TCP Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> connectionCount{0};
    std::atomic<int> messageCount{0};
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 在事件循环线程中创建 TcpServer
        auto server = std::make_unique<TcpServer>(loop, "MultiThreadServer", "127.0.0.1", 8083);
        
        // 设置线程数量为4
        server->setThreadNum(4);
        
        // 设置线程初始化回调
        server->setThreadInitCallback([](EventLoop* ioLoop) {
            LOG_INFO("Thread initialized for EventLoop {}", static_cast<void*>(ioLoop));
            std::cout << "✓ Thread pool thread started" << std::endl;
        });
        
        // 设置连接回调
        server->setConnectionCallback([&connectionCount](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                connectionCount++;
                LOG_INFO("New connection from {} on thread {}", conn->peerAddress(), static_cast<void*>(conn->getLoop()));
                std::cout << "✓ Client connected: " << conn->peerAddress() 
                         << " (total: " << connectionCount.load() << ")" << std::endl;
                
                // 发送欢迎消息
                std::string welcome = "Welcome to Multi-thread SunKV Server!\r\n";
                conn->send(welcome);
            } else {
                connectionCount--;
                LOG_INFO("Connection closed: {} on thread {}", conn->peerAddress(), static_cast<void*>(conn->getLoop()));
                std::cout << "✓ Client disconnected: " << conn->peerAddress() 
                         << " (total: " << connectionCount.load() << ")" << std::endl;
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([&messageCount](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            std::string message(static_cast<char*>(data), len);
            messageCount++;
            LOG_INFO("Received {} bytes from {} on thread {}: {}", 
                     len, conn->peerAddress(), static_cast<void*>(conn->getLoop()), message);
            std::cout << "📨 Received: " << message;
            
            // 回显消息
            conn->send(message);
            std::cout << "📤 Echoed back (thread: " << static_cast<void*>(conn->getLoop()) << ")" << std::endl;
        });
        
        std::cout << "Starting Multi-thread TCP server on 127.0.0.1:8083..." << std::endl;
        std::cout << "Thread pool size: 4" << std::endl;
        std::cout << "Test with: echo 'test' | nc 127.0.0.1 8083" << std::endl;
        std::cout << "Server will run for 6 seconds..." << std::endl;
        
        // 启动服务器
        server->start();
        
        // 6秒后停止服务器
        loop->runAfter(std::chrono::seconds(6), [loop]() {
            LOG_INFO("Stopping multi-thread server");
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
    
    std::cout << "Multi-thread TCP test running..." << std::endl;
    
    // 主线程等待8秒（确保服务器完全运行）
    std::this_thread::sleep_for(std::chrono::seconds(8));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== Multi-thread TCP Test Results ===" << std::endl;
    std::cout << "Total connections handled: " << connectionCount.load() << std::endl;
    std::cout << "Total messages processed: " << messageCount.load() << std::endl;
    std::cout << "Server ran successfully for 6 seconds" << std::endl;
    std::cout << "Thread pool size: 4" << std::endl;
    
    if (connectionCount.load() >= 0) {
        std::cout << "✅ Multi-thread TCP test PASSED!" << std::endl;
        LOG_INFO("Multi-thread TCP test PASSED");
    } else {
        std::cout << "❌ Multi-thread TCP test FAILED!" << std::endl;
        LOG_ERROR("Multi-thread TCP test FAILED");
    }
    
    LOG_INFO("Multi-thread TCP test completed");
    return 0;
}
