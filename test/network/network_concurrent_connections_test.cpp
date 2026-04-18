#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"

// 客户端连接测试函数
void clientConnection(int clientId, const std::string& serverAddrStr, uint16_t serverPort, 
                     std::atomic<int>& successCount, std::atomic<int>& failCount) {
    // 创建 socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        failCount++;
        return;
    }
    
    // 设置服务器地址
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverAddrStr.c_str(), &serverAddr.sin_addr);
    
    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sockfd);
        failCount++;
        return;
    }
    
    // 发送测试消息
    std::string message = "Hello from client " + std::to_string(clientId) + "\n";
    if (send(sockfd, message.c_str(), message.length(), 0) < 0) {
        close(sockfd);
        failCount++;
        return;
    }
    
    // 接收响应
    char buffer[1024];
    int n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        successCount++;
    } else {
        failCount++;
    }
    
    close(sockfd);
}

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Concurrent Connection test starting...");
    
    std::cout << "=== Concurrent Connection Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> connectionCount{0};
    std::atomic<int> messageCount{0};
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 在事件循环线程中创建 TcpServer
        auto server = std::make_unique<TcpServer>(loop, "ConcurrentTestServer", "127.0.0.1", 8085);
        
        // 设置线程数量为4
        server->setThreadNum(4);
        
        // 设置连接回调
        server->setConnectionCallback([&connectionCount](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                connectionCount++;
                LOG_INFO("Concurrent client connected: {} (total: {})", conn->peerAddress(), connectionCount.load());
            } else {
                connectionCount--;
                LOG_INFO("Concurrent client disconnected: {} (total: {})", conn->peerAddress(), connectionCount.load());
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([&messageCount](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            messageCount++;
            std::string message(static_cast<char*>(data), len);
            LOG_INFO("Concurrent received: {}", message);
            
            // 回显消息
            conn->send(message);
        });
        
        std::cout << "Starting Concurrent Test Server on 127.0.0.1:8085..." << std::endl;
        std::cout << "Thread pool size: 4" << std::endl;
        
        // 启动服务器
        server->start();
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 在事件循环线程中先销毁 TcpServer，再销毁 EventLoop
        server.reset();
        delete loop;
        loop = nullptr;
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Starting concurrent connection test..." << std::endl;
    
    // 并发连接测试
    const int numClients = 20;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    
    std::vector<std::thread> clientThreads;
    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back(clientConnection, i, "127.0.0.1", 8085, 
                                  std::ref(successCount), std::ref(failCount));
    }
    
    // 等待所有客户端完成
    for (auto& t : clientThreads) {
        t.join();
    }
    
    std::cout << "Concurrent connection test completed!" << std::endl;
    std::cout << "Total clients: " << numClients << std::endl;
    std::cout << "Successful connections: " << successCount.load() << std::endl;
    std::cout << "Failed connections: " << failCount.load() << std::endl;
    
    // 等待服务器处理完所有连接
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 停止服务器
    std::cout << "Stopping server..." << std::endl;
    loop->quit();
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== Concurrent Connection Test Results ===" << std::endl;
    std::cout << "Total connections handled: " << connectionCount.load() << std::endl;
    std::cout << "Total messages processed: " << messageCount.load() << std::endl;
    std::cout << "Success rate: " << (successCount.load() * 100 / numClients) << "%" << std::endl;
    
    if (successCount.load() >= numClients * 0.8) { // 80% 成功率
        std::cout << "✅ Concurrent Connection test PASSED!" << std::endl;
        LOG_INFO("Concurrent Connection test PASSED");
    } else {
        std::cout << "❌ Concurrent Connection test FAILED!" << std::endl;
        LOG_ERROR("Concurrent Connection test FAILED");
    }
    
    LOG_INFO("Concurrent Connection test completed");
    return 0;
}
