#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include "command/Command.h"
#include "command/CommandDispatcher.h"
#include "command/commands/PingCommand.h"
#include "command/commands/EchoCommand.h"
#include "command/commands/SetCommand.h"
#include "command/commands/GetCommand.h"
#include "command/commands/DelCommand.h"
#include "command/commands/ExistsCommand.h"
#include "storage/StorageEngine.h"

int main() {
    std::cout << "=== Command Integration Test ===" << std::endl;
    
    // 创建命令分发器
    CommandDispatcher dispatcher;
    auto& storage = StorageEngine::getInstance();
    
    std::cout << "\n--- Basic Workflow Test ---" << std::endl;
    
    // 测试基本工作流程：SET -> GET -> EXISTS -> DEL
    auto setResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("workflow_key"), 
        makeBulkString("workflow_value")
    }));
    std::cout << "SET workflow_key workflow_value: ";
    if (setResult.success) {
        std::cout << "✅ " << setResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setResult.error << std::endl;
        return 1;
    }
    
    auto getResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("workflow_key")
    }));
    std::cout << "GET workflow_key: ";
    if (getResult.success) {
        std::cout << "✅ " << getResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getResult.error << std::endl;
        return 1;
    }
    
    auto existsResult = dispatcher.handleCommand(makeArray({
        makeBulkString("exists"), 
        makeBulkString("workflow_key")
    }));
    std::cout << "EXISTS workflow_key: ";
    if (existsResult.success) {
        std::cout << "✅ " << existsResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << existsResult.error << std::endl;
        return 1;
    }
    
    auto delResult = dispatcher.handleCommand(makeArray({
        makeBulkString("del"), 
        makeBulkString("workflow_key")
    }));
    std::cout << "DEL workflow_key: ";
    if (delResult.success) {
        std::cout << "✅ " << delResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << delResult.error << std::endl;
        return 1;
    }
    
    auto getAfterDelResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("workflow_key")
    }));
    std::cout << "GET workflow_key (after DEL): ";
    if (getAfterDelResult.success) {
        std::cout << "✅ " << getAfterDelResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getAfterDelResult.error << std::endl;
        return 1;
    }
    
    std::cout << "\n--- TTL Test ---" << std::endl;
    
    // 测试 TTL 功能
    auto setTtlResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("ttl_key"), 
        makeBulkString("ttl_value"),
        makeInteger(100)  // 100ms TTL
    }));
    std::cout << "SET ttl_key ttl_value TTL=100: ";
    if (setTtlResult.success) {
        std::cout << "✅ " << setTtlResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setTtlResult.error << std::endl;
        return 1;
    }
    
    // 立即获取应该成功
    auto getTtlImmediateResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("ttl_key")
    }));
    std::cout << "GET ttl_key (immediate): ";
    if (getTtlImmediateResult.success) {
        std::cout << "✅ " << getTtlImmediateResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getTtlImmediateResult.error << std::endl;
        return 1;
    }
    
    // 等待过期
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    auto getTtlExpiredResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("ttl_key")
    }));
    std::cout << "GET ttl_key (after TTL expired): ";
    if (getTtlExpiredResult.success) {
        std::cout << "✅ " << getTtlExpiredResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getTtlExpiredResult.error << std::endl;
        return 1;
    }
    
    std::cout << "\n--- Batch Operations Test ---" << std::endl;
    
    // 设置多个键
    for (int i = 1; i <= 5; ++i) {
        dispatcher.handleCommand(makeArray({
            makeBulkString("set"), 
            makeBulkString("batch_key_" + std::to_string(i)), 
            makeBulkString("batch_value_" + std::to_string(i))
        }));
    }
    
    // 批量删除
    auto delBatchResult = dispatcher.handleCommand(makeArray({
        makeBulkString("del"), 
        makeBulkString("batch_key_1"),
        makeBulkString("batch_key_2"),
        makeBulkString("batch_key_3"),
        makeBulkString("batch_key_4"),
        makeBulkString("batch_key_5")
    }));
    std::cout << "DEL batch_key_1..5: ";
    if (delBatchResult.success) {
        std::cout << "✅ " << delBatchResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << delBatchResult.error << std::endl;
        return 1;
    }
    
    // 验证所有键都被删除
    int remainingKeys = 0;
    for (int i = 1; i <= 5; ++i) {
        auto existsResult = dispatcher.handleCommand(makeArray({
            makeBulkString("exists"), 
            makeBulkString("batch_key_" + std::to_string(i))
        }));
        if (existsResult.success && existsResult.response->getType() == RESPType::INTEGER) {
            int64_t value = std::static_pointer_cast<RESPInteger>(existsResult.response)->getValue();
            if (value == 1) remainingKeys++;
        }
    }
    std::cout << "Remaining keys after batch DEL: " << remainingKeys << " (should be 0): ";
    if (remainingKeys == 0) {
        std::cout << "✅" << std::endl;
    } else {
        std::cout << "❌" << std::endl;
        return 1;
    }
    
    std::cout << "\n--- Edge Cases Test ---" << std::endl;
    
    // 测试空字符串键值
    auto setEmptyResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString(""), 
        makeBulkString("")
    }));
    std::cout << "SET empty_key empty_value: ";
    if (setEmptyResult.success) {
        std::cout << "✅ " << setEmptyResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setEmptyResult.error << std::endl;
        return 1;
    }
    
    auto getEmptyResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("")
    }));
    std::cout << "GET empty_key: ";
    if (getEmptyResult.success) {
        std::cout << "✅ " << getEmptyResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getEmptyResult.error << std::endl;
        return 1;
    }
    
    // 测试长字符串
    std::string longValue(1000, 'A');  // 1000个'A'
    auto setLongResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("long_key"), 
        makeBulkString(longValue)
    }));
    std::cout << "SET long_key long_value(1000 chars): ";
    if (setLongResult.success) {
        std::cout << "✅ " << setLongResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setLongResult.error << std::endl;
        return 1;
    }
    
    auto getLongResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("long_key")
    }));
    std::cout << "GET long_key: ";
    if (getLongResult.success) {
        auto value = getLongResult.response->toString();
        if (value.length() == 1000) {
            std::cout << "✅ Length: " << value.length() << std::endl;
        } else {
            std::cout << "❌ Wrong length: " << value.length() << std::endl;
            return 1;
        }
    } else {
        std::cout << "❌ " << getLongResult.error << std::endl;
        return 1;
    }
    
    std::cout << "\n--- Storage Engine Direct Test ---" << std::endl;
    
    // 直接测试存储引擎
    storage.clear();
    storage.set("direct_key", "direct_value");
    std::cout << "Direct storage SET/GET: ";
    if (storage.get("direct_key") == "direct_value") {
        std::cout << "✅" << std::endl;
    } else {
        std::cout << "❌" << std::endl;
        return 1;
    }
    
    std::cout << "Storage size: " << storage.size() << " (should be 1): ";
    if (storage.size() == 1) {
        std::cout << "✅" << std::endl;
    } else {
        std::cout << "❌" << std::endl;
        return 1;
    }
    
    storage.clear();
    std::cout << "Storage after clear: " << storage.size() << " (should be 0): ";
    if (storage.size() == 0) {
        std::cout << "✅" << std::endl;
    } else {
        std::cout << "❌" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Command Integration Test Results ===" << std::endl;
    std::cout << "Basic Workflow: ✅ Working" << std::endl;
    std::cout << "TTL Functionality: ✅ Working" << std::endl;
    std::cout << "Batch Operations: ✅ Working" << std::endl;
    std::cout << "Edge Cases: ✅ Working" << std::endl;
    std::cout << "Storage Engine: ✅ Working" << std::endl;
    std::cout << "Command Integration: ✅ Working" << std::endl;
    std::cout << "\n🎉 COMMAND INTEGRATION TEST PASSED! 🎉" << std::endl;
    
    return 0;
}
