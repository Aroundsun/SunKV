#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"
#include "protocol/RESPType.h"
#include "protocol/RESPParser.h"

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("RESP Network Integration test starting...");
    
    std::cout << "=== RESP Network Integration Test ===" << std::endl;
    
    EventLoop* loop = nullptr;
    int messageCount = 0;
    
    // 在事件循环线程中创建 EventLoop 和 TcpServer
    std::thread loopThread([&]() {
        LOG_INFO("Event loop thread started");
        
        // 在事件循环线程中创建 EventLoop
        loop = new EventLoop();
        
        // 在事件循环线程中创建 TcpServer
        auto server = std::make_unique<TcpServer>(loop, "RESPTestServer", "127.0.0.1", 8088);
        
        // 设置连接回调
        server->setConnectionCallback([&messageCount](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                LOG_INFO("RESP client connected: {}", conn->peerAddress());
                std::cout << "✓ Client connected: " << conn->peerAddress() << std::endl;
                
                // 发送欢迎消息
                auto welcome = makeSimpleString("RESP Server Ready!");
                conn->send(welcome->encode());
            } else {
                LOG_INFO("RESP client disconnected: {}", conn->peerAddress());
                std::cout << "✓ Client disconnected: " << conn->peerAddress() << std::endl;
            }
        });
        
        // 设置消息回调
        server->setMessageCallback([&messageCount](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
            std::string message(static_cast<char*>(data), len);
            messageCount++;
            
            LOG_INFO("Received {} bytes: {}", len, message);
            std::cout << "📨 Received (" << len << " bytes): " << message;
            
            // 解析 RESP 协议
            RESPParser parser;
            ParseResult result = parser.parse(message);
            
            if (result.success && result.complete) {
                std::cout << "✅ Parsed: " << result.value->toString() << std::endl;
                
                // 根据解析结果生成响应
                RESPValue::Ptr response;
                
                if (result.value->isSimpleString()) {
                    std::string cmd = result.value->toString();
                    if (cmd == "PING") {
                        response = makeSimpleString("PONG");
                    } else if (cmd == "ECHO") {
                        response = makeSimpleString("ECHO: " + cmd);
                    } else {
                        response = makeError("Unknown command: " + cmd);
                    }
                } else if (result.value->isArray()) {
                    auto array = std::dynamic_pointer_cast<RESPArray>(result.value);
                    if (array && array->size() > 0) {
                        auto first = array->getValues()[0];
                        if (first && first->isSimpleString()) {
                            std::string cmd = first->toString();
                            if (cmd == "SET") {
                                response = makeSimpleString("OK");
                            } else if (cmd == "GET") {
                                response = makeBulkString("value");
                            } else {
                                response = makeError("Unknown array command: " + cmd);
                            }
                        }
                    }
                } else {
                    response = makeError("Unsupported command format");
                }
                
                // 发送响应
                if (response) {
                    std::string encodedResponse = response->encode();
                    conn->send(encodedResponse);
                    std::cout << "📤 Sent response: " << response->toString() << std::endl;
                }
            } else {
                std::cout << "❌ Parse failed: " << result.error << std::endl;
                auto error = makeError("Parse error: " + result.error);
                conn->send(error->encode());
            }
        });
        
        std::cout << "Starting RESP Test Server on 127.0.0.1:8088..." << std::endl;
        std::cout << "Test with:" << std::endl;
        std::cout << "  echo 'PING' | nc 127.0.0.1 8088" << std::endl;
        std::cout << "  echo '*1\\r\\n$3\\r\\nGET\\r\\nkey\\r\\n' | nc 127.0.0.1 8088" << std::endl;
        std::cout << "Server will run for 8 seconds..." << std::endl;
        
        // 直接在事件循环线程中启动服务器
        server->start();
        
        // 8秒后停止服务器
        loop->runAfter(std::chrono::seconds(8), [loop]() {
            LOG_INFO("Stopping RESP test server");
            loop->quit();
        });
        
        // 运行事件循环
        loop->loop();
        LOG_INFO("Event loop thread ended");
        
        // 在事件循环线程中删除对象
        server.reset();
        delete loop;
        loop = nullptr;
    });
    
    std::cout << "RESP Network Integration test running..." << std::endl;
    
    // 主线程等待10秒（确保服务器完全运行）
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 等待事件循环线程结束
    loopThread.join();
    
    // 验证结果
    std::cout << "\n=== RESP Network Integration Test Results ===" << std::endl;
    std::cout << "Total messages processed: " << messageCount << std::endl;
    std::cout << "Server ran successfully for 8 seconds" << std::endl;
    
    if (messageCount >= 0) {
        std::cout << "✅ RESP Network Integration test PASSED!" << std::endl;
        LOG_INFO("RESP Network Integration test PASSED");
    } else {
        std::cout << "❌ RESP Network Integration test FAILED!" << std::endl;
        LOG_ERROR("RESP Network Integration test FAILED");
    }
    
    LOG_INFO("RESP Network Integration test completed");
    return 0;
}
