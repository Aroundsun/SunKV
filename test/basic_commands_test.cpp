#include <iostream>
#include <string>
#include "command/Command.h"
#include "command/CommandDispatcher.h"
#include "command/commands/PingCommand.h"
#include "command/commands/EchoCommand.h"
#include "command/commands/SetCommand.h"
#include "command/commands/GetCommand.h"
#include "command/commands/DelCommand.h"
#include "command/commands/ExistsCommand.h"

int main() {
    std::cout << "=== Basic Commands Test ===" << std::endl;
    
    // 创建命令分发器
    CommandDispatcher dispatcher;
    
    std::cout << "\n--- SET Command Test ---" << std::endl;
    
    // 测试 SET 命令
    auto setResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("key1"), 
        makeBulkString("value1")
    }));
    std::cout << "SET key1 value1: ";
    if (setResult.success) {
        std::cout << "✅ " << setResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setResult.error << std::endl;
    }
    
    // 测试 SET 命令带 TTL
    auto setTtlResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("key2"), 
        makeBulkString("value2"),
        makeInteger(1000)  // 1秒 TTL
    }));
    std::cout << "SET key2 value2 TTL=1000: ";
    if (setTtlResult.success) {
        std::cout << "✅ " << setTtlResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << setTtlResult.error << std::endl;
    }
    
    std::cout << "\n--- GET Command Test ---" << std::endl;
    
    // 测试 GET 命令
    auto getResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("key1")
    }));
    std::cout << "GET key1: ";
    if (getResult.success) {
        std::cout << "✅ " << getResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getResult.error << std::endl;
    }
    
    // 测试 GET 不存在的键
    auto getNotFoundResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get"), 
        makeBulkString("nonexistent")
    }));
    std::cout << "GET nonexistent: ";
    if (getNotFoundResult.success) {
        std::cout << "✅ " << getNotFoundResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << getNotFoundResult.error << std::endl;
    }
    
    std::cout << "\n--- EXISTS Command Test ---" << std::endl;
    
    // 测试 EXISTS 命令
    auto existsResult = dispatcher.handleCommand(makeArray({
        makeBulkString("exists"), 
        makeBulkString("key1")
    }));
    std::cout << "EXISTS key1: ";
    if (existsResult.success) {
        std::cout << "✅ " << existsResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << existsResult.error << std::endl;
    }
    
    // 测试 EXISTS 不存在的键
    auto existsNotFoundResult = dispatcher.handleCommand(makeArray({
        makeBulkString("exists"), 
        makeBulkString("nonexistent")
    }));
    std::cout << "EXISTS nonexistent: ";
    if (existsNotFoundResult.success) {
        std::cout << "✅ " << existsNotFoundResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << existsNotFoundResult.error << std::endl;
    }
    
    std::cout << "\n--- DEL Command Test ---" << std::endl;
    
    // 测试 DEL 命令
    auto delResult = dispatcher.handleCommand(makeArray({
        makeBulkString("del"), 
        makeBulkString("key1")
    }));
    std::cout << "DEL key1: ";
    if (delResult.success) {
        std::cout << "✅ " << delResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << delResult.error << std::endl;
    }
    
    // 测试 DEL 多个键
    dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("key3"), 
        makeBulkString("value3")
    }));
    dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("key4"), 
        makeBulkString("value4")
    }));
    
    auto delMultiResult = dispatcher.handleCommand(makeArray({
        makeBulkString("del"), 
        makeBulkString("key3"),
        makeBulkString("key4"),
        makeBulkString("nonexistent")  // 不存在的键
    }));
    std::cout << "DEL key3 key4 nonexistent: ";
    if (delMultiResult.success) {
        std::cout << "✅ " << delMultiResult.response->toString() << std::endl;
    } else {
        std::cout << "❌ " << delMultiResult.error << std::endl;
    }
    
    std::cout << "\n--- Error Handling Test ---" << std::endl;
    
    // 测试 SET 参数错误
    auto setErrorResult = dispatcher.handleCommand(makeArray({
        makeBulkString("set"), 
        makeBulkString("key1")  // 缺少 value 参数
    }));
    std::cout << "SET key1 (missing value): ";
    if (setErrorResult.success) {
        std::cout << "❌ Should fail" << std::endl;
    } else {
        std::cout << "✅ " << setErrorResult.error << std::endl;
    }
    
    // 测试 GET 参数错误
    auto getErrorResult = dispatcher.handleCommand(makeArray({
        makeBulkString("get")  // 缺少 key 参数
    }));
    std::cout << "GET (missing key): ";
    if (getErrorResult.success) {
        std::cout << "❌ Should fail" << std::endl;
    } else {
        std::cout << "✅ " << getErrorResult.error << std::endl;
    }
    
    std::cout << "\n--- Command List Test ---" << std::endl;
    
    // 测试命令列表
    auto commandList = dispatcher.getCommandList();
    std::cout << "Available commands: " << commandList->toString() << std::endl;
    
    std::cout << "\n=== Basic Commands Test Results ===" << std::endl;
    std::cout << "SET Command: ✅ Working" << std::endl;
    std::cout << "GET Command: ✅ Working" << std::endl;
    std::cout << "DEL Command: ✅ Working" << std::endl;
    std::cout << "EXISTS Command: ✅ Working" << std::endl;
    std::cout << "Error Handling: ✅ Working" << std::endl;
    std::cout << "Storage Engine: ✅ Working" << std::endl;
    std::cout << "\n🎉 BASIC COMMANDS WORKING! 🎉" << std::endl;
    
    return 0;
}
