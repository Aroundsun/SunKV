#include <iostream>
#include <thread>
#include <chrono>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("TCP Server test starting...");
    
    std::cout << "=== TCP Server Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 创建 TCP 服务器（在事件循环线程中）
        TcpServer* server = new TcpServer(loop, "TestServer", "127.0.0.1", 8080);
        
        // 设置连接回调
        server->setConnectionCallback([](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                LOG_INFO("New connection from {}", conn->peerAddress());
                std::cout << "✓ New connection established: " << conn->peerAddress() << std::endl;
                
                // 发送欢迎消息
                std::string welcome = "Welcome to SunKV Server!\r\n";
                conn->send(welcome);
            } else {
                LOG_INFO("Connection closed: {}", conn->peerAddress());
                std::cout << "✓ Connection closed: " << conn->peerAddress() << std::endl;
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            std::string message(static_cast<char*>(data), len);
            LOG_INFO("Received {} bytes from {}: {}", len, conn->peerAddress(), message);
            std::cout << "✓ Received from " << conn->peerAddress() << ": " << message;
            
            // 回显消息
            std::string echo = "Echo: " + message + "\r\n";
            conn->send(echo);
        });
        
        // 设置写入完成回调
        server->setWriteCompleteCallback([](const std::shared_ptr<TcpConnection>& conn) {
            LOG_DEBUG("Write complete for {}", conn->peerAddress());
        });
        
        std::cout << "Starting TCP server on 127.0.0.1:8080..." << std::endl;
        std::cout << "Connect using: telnet 127.0.0.1 8080" << std::endl;
        std::cout << "Server will run for 10 seconds..." << std::endl;
        
        // 启动服务器
        server->start();
        
        // 10秒后停止服务器
        loop->runAfter(std::chrono::seconds(10), [loop]() {
            LOG_INFO("Stopping server");
            loop->quit();
        });
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 在事件循环线程中删除对象
        delete server;
        delete loop;
        loop = nullptr;
    });
    
    std::cout << "TCP Server test running..." << std::endl;
    
    // 主线程等待12秒（确保服务器完全运行）
    std::this_thread::sleep_for(std::chrono::seconds(12));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    std::cout << "TCP Server test completed!" << std::endl;
    LOG_INFO("TCP Server test completed");
    
    return 0;
}
