#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"

// 性能测试客户端
class BenchmarkClient {
public:
    BenchmarkClient(int id, const std::string& serverAddr, uint16_t serverPort)
        : id_(id), serverAddr_(serverAddr), serverPort_(serverPort) {}
    
    void run(std::atomic<int>& successCount, std::atomic<int>& failCount, 
             std::atomic<long long>& totalBytes, std::atomic<int>& totalMessages) {
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
        serverAddr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverAddr_.c_str(), &serverAddr.sin_addr);
        
        // 连接服务器
        if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            close(sockfd);
            failCount++;
            return;
        }
        
        // 发送多个消息
        const int numMessages = 10;
        const std::string message = "Benchmark message from client " + std::to_string(id_) + " - ";
        
        for (int i = 0; i < numMessages; ++i) {
            std::string fullMessage = message + std::to_string(i) + "\n";
            
            if (send(sockfd, fullMessage.c_str(), fullMessage.length(), 0) < 0) {
                break;
            }
            
            // 接收响应
            char buffer[1024];
            int n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                totalBytes += n;
                totalMessages++;
            }
        }
        
        successCount++;
        close(sockfd);
    }
    
private:
    int id_;
    std::string serverAddr_;
    uint16_t serverPort_;
};

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Performance Benchmark test starting...");
    
    std::cout << "=== Performance Benchmark Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    std::atomic<int> connectionCount{0};
    std::atomic<int> messageCount{0};
    std::atomic<long long> bytesReceived{0};
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 在事件循环线程中创建 TcpServer
        auto server = std::make_unique<TcpServer>(loop, "BenchmarkServer", "127.0.0.1", 8087);
        
        // 设置线程数量为4
        server->setThreadNum(4);
        
        // 设置连接回调
        server->setConnectionCallback([&connectionCount](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                connectionCount++;
                LOG_INFO("Benchmark client connected: {} (total: {})", conn->peerAddress(), connectionCount.load());
            } else {
                connectionCount--;
                LOG_INFO("Benchmark client disconnected: {} (total: {})", conn->peerAddress(), connectionCount.load());
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([&messageCount, &bytesReceived](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            messageCount++;
            bytesReceived += len;
            
            // 回显消息
            conn->send(data, len);
        });
        
        std::cout << "Starting Benchmark Server on 127.0.0.1:8087..." << std::endl;
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
    
    std::cout << "Starting performance benchmark..." << std::endl;
    
    // 性能测试参数
    const int numClients = 50;
    const int messagesPerClient = 10;
    const int totalExpectedMessages = numClients * messagesPerClient;
    
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    std::atomic<long long> totalBytes{0};
    std::atomic<int> totalMessages{0};
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // 创建并发客户端
    std::vector<std::thread> clientThreads;
    std::vector<std::unique_ptr<BenchmarkClient>> clients;
    
    for (int i = 0; i < numClients; ++i) {
        auto client = std::make_unique<BenchmarkClient>(i, "127.0.0.1", 8087);
        clients.push_back(std::move(client));
        
        clientThreads.emplace_back([&clients, i, &successCount, &failCount, &totalBytes, &totalMessages]() {
            clients[i]->run(successCount, failCount, totalBytes, totalMessages);
        });
    }
    
    // 等待所有客户端完成
    for (auto& t : clientThreads) {
        t.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "Performance benchmark completed!" << std::endl;
    
    // 等待服务器处理完所有消息
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 停止服务器
    std::cout << "Stopping server..." << std::endl;
    loop->quit();
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 计算性能指标
    double throughput = (totalBytes.load() * 1000.0) / duration.count(); // bytes per second
    double messagesPerSecond = (totalMessages.load() * 1000.0) / duration.count();
    double connectionsPerSecond = (successCount.load() * 1000.0) / duration.count();
    
    // 验证结果
    std::cout << "\n=== Performance Benchmark Results ===" << std::endl;
    std::cout << "Test duration: " << duration.count() << " ms" << std::endl;
    std::cout << "Total clients: " << numClients << std::endl;
    std::cout << "Successful connections: " << successCount.load() << std::endl;
    std::cout << "Failed connections: " << failCount.load() << std::endl;
    std::cout << "Connection success rate: " << (successCount.load() * 100.0 / numClients) << "%" << std::endl;
    std::cout << "Total messages processed: " << messageCount.load() << std::endl;
    std::cout << "Total bytes received: " << bytesReceived.load() << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << throughput << " bytes/sec" << std::endl;
    std::cout << "Messages per second: " << std::fixed << std::setprecision(2) << messagesPerSecond << std::endl;
    std::cout << "Connections per second: " << std::fixed << std::setprecision(2) << connectionsPerSecond << std::endl;
    
    // 性能基准
    bool performanceGood = successCount.load() >= numClients * 0.9 && // 90% 连接成功率
                          messageCount.load() >= totalExpectedMessages * 0.9 && // 90% 消息处理率
                          throughput > 10000; // 至少 10KB/s
    
    if (performanceGood) {
        std::cout << "✅ Performance Benchmark test PASSED!" << std::endl;
        LOG_INFO("Performance Benchmark test PASSED");
    } else {
        std::cout << "❌ Performance Benchmark test FAILED!" << std::endl;
        LOG_ERROR("Performance Benchmark test FAILED");
    }
    
    LOG_INFO("Performance Benchmark test completed");
    return 0;
}
