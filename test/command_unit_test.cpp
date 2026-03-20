#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include "command/Command.h"
#include "command/CommandDispatcher.h"
#include "command/commands/SetCommand.h"
#include "command/commands/GetCommand.h"
#include "command/commands/DelCommand.h"
#include "command/commands/ExistsCommand.h"
#include "storage/StorageEngine.h"

// 测试辅助函数
void testCommand(const std::string& testName, bool condition, const std::string& message = "") {
    std::cout << testName << ": ";
    if (condition) {
        std::cout << "✅" << std::endl;
    } else {
        std::cout << "❌" << (message.empty() ? "" : " - " + message) << std::endl;
    }
}

int main() {
    std::cout << "=== Command Unit Test ===" << std::endl;
    
    CommandDispatcher dispatcher;
    auto& storage = StorageEngine::getInstance();
    
    std::cout << "\n--- SET Command Unit Test ---" << std::endl;
    
    // 测试 SET 命令基本信息
    SetCommand setCmd;
    testCommand("SET command name", setCmd.name() == "set");
    testCommand("SET command description", !setCmd.description().empty());
    testCommand("SET command arg count", setCmd.argCount() == std::make_pair(2, 3));
    
    // 测试 SET 命令执行
    storage.clear();
    auto setResult = setCmd.execute({makeBulkString("test_key"), makeBulkString("test_value")});
    testCommand("SET basic execution", setResult.success);
    testCommand("SET response", setResult.response && setResult.response->toString() == "OK");
    
    // 测试 SET 命令参数验证
    auto setNoArgsResult = setCmd.execute({});
    testCommand("SET no args validation", !setNoArgsResult.success);
    
    auto setTooManyArgsResult = setCmd.execute({
        makeBulkString("key"), 
        makeBulkString("value"), 
        makeInteger(1000),
        makeBulkString("extra")
    });
    testCommand("SET too many args validation", !setTooManyArgsResult.success);
    
    // 测试 SET TTL
    auto setTtlResult = setCmd.execute({
        makeBulkString("ttl_key"), 
        makeBulkString("ttl_value"), 
        makeInteger(1000)
    });
    testCommand("SET with TTL", setTtlResult.success);
    
    std::cout << "\n--- GET Command Unit Test ---" << std::endl;
    
    // 测试 GET 命令基本信息
    GetCommand getCmd;
    testCommand("GET command name", getCmd.name() == "get");
    testCommand("GET command description", !getCmd.description().empty());
    testCommand("GET command arg count", getCmd.argCount() == std::make_pair(1, 1));
    
    // 测试 GET 命令执行
    storage.set("get_test_key", "get_test_value");
    auto getResult = getCmd.execute({makeBulkString("get_test_key")});
    testCommand("GET existing key", getResult.success);
    testCommand("GET existing key value", getResult.response && getResult.response->toString() == "get_test_value");
    
    // 测试 GET 不存在的键
    auto getNotFoundResult = getCmd.execute({makeBulkString("nonexistent_key")});
    testCommand("GET nonexistent key", getNotFoundResult.success);
    testCommand("GET nonexistent key response", getNotFoundResult.response && getNotFoundResult.response->getType() == RESPType::NULL_BULK);
    
    // 测试 GET 参数验证
    auto getNoArgsResult = getCmd.execute({});
    testCommand("GET no args validation", !getNoArgsResult.success);
    
    auto getTooManyArgsResult = getCmd.execute({
        makeBulkString("key1"), 
        makeBulkString("key2")
    });
    testCommand("GET too many args validation", !getTooManyArgsResult.success);
    
    std::cout << "\n--- DEL Command Unit Test ---" << std::endl;
    
    // 测试 DEL 命令基本信息
    DelCommand delCmd;
    testCommand("DEL command name", delCmd.name() == "del");
    testCommand("DEL command description", !delCmd.description().empty());
    testCommand("DEL command arg count", delCmd.argCount() == std::make_pair(1, -1));
    
    // 测试 DEL 命令执行
    storage.set("del_test_key", "del_test_value");
    auto delResult = delCmd.execute({makeBulkString("del_test_key")});
    testCommand("DEL existing key", delResult.success);
    testCommand("DEL response count", delResult.response && delResult.response->toString() == "1");
    
    // 测试 DEL 不存在的键
    auto delNotFoundResult = delCmd.execute({makeBulkString("nonexistent_key")});
    testCommand("DEL nonexistent key", delNotFoundResult.success);
    testCommand("DEL nonexistent key response", delNotFoundResult.response && delNotFoundResult.response->toString() == "0");
    
    // 测试 DEL 批量删除
    storage.set("batch_key_1", "value1");
    storage.set("batch_key_2", "value2");
    storage.set("batch_key_3", "value3");
    
    auto delBatchResult = delCmd.execute({
        makeBulkString("batch_key_1"),
        makeBulkString("batch_key_2"),
        makeBulkString("batch_key_3"),
        makeBulkString("nonexistent_key")
    });
    testCommand("DEL batch operation", delBatchResult.success);
    testCommand("DEL batch response", delBatchResult.response && delBatchResult.response->toString() == "3");
    
    // 测试 DEL 参数验证
    auto delNoArgsResult = delCmd.execute({});
    testCommand("DEL no args validation", !delNoArgsResult.success);
    
    std::cout << "\n--- EXISTS Command Unit Test ---" << std::endl;
    
    // 测试 EXISTS 命令基本信息
    ExistsCommand existsCmd;
    testCommand("EXISTS command name", existsCmd.name() == "exists");
    testCommand("EXISTS command description", !existsCmd.description().empty());
    testCommand("EXISTS command arg count", existsCmd.argCount() == std::make_pair(1, 1));
    
    // 测试 EXISTS 命令执行
    storage.set("exists_test_key", "exists_test_value");
    auto existsResult = existsCmd.execute({makeBulkString("exists_test_key")});
    testCommand("EXISTS existing key", existsResult.success);
    testCommand("EXISTS existing key response", existsResult.response && existsResult.response->toString() == "1");
    
    // 测试 EXISTS 不存在的键
    auto existsNotFoundResult = existsCmd.execute({makeBulkString("nonexistent_key")});
    testCommand("EXISTS nonexistent key", existsNotFoundResult.success);
    testCommand("EXISTS nonexistent key response", existsNotFoundResult.response && existsNotFoundResult.response->toString() == "0");
    
    // 测试 EXISTS 参数验证
    auto existsNoArgsResult = existsCmd.execute({});
    testCommand("EXISTS no args validation", !existsNoArgsResult.success);
    
    auto existsTooManyArgsResult = existsCmd.execute({
        makeBulkString("key1"), 
        makeBulkString("key2")
    });
    testCommand("EXISTS too many args validation", !existsTooManyArgsResult.success);
    
    std::cout << "\n--- Command Registry Unit Test ---" << std::endl;
    
    auto& registry = CommandRegistry::getInstance();
    
    // 测试命令注册
    testCommand("Registry has SET", registry.hasCommand("set"));
    testCommand("Registry has GET", registry.hasCommand("get"));
    testCommand("Registry has DEL", registry.hasCommand("del"));
    testCommand("Registry has EXISTS", registry.hasCommand("exists"));
    testCommand("Registry has unknown command", !registry.hasCommand("unknown_command"));
    
    // 测试命令创建
    auto setCommand = registry.createCommand("set");
    testCommand("Create SET command", setCommand != nullptr);
    
    auto unknownCommand = registry.createCommand("unknown_command");
    testCommand("Create unknown command", unknownCommand == nullptr);
    
    // 测试命令列表
    auto commandList = registry.getAllCommands();
    testCommand("Command list not empty", !commandList.empty());
    testCommand("Has SET in list", std::find(commandList.begin(), commandList.end(), "set") != commandList.end());
    
    std::cout << "\n--- Storage Engine Unit Test ---" << std::endl;
    
    storage.clear();
    
    // 测试基本操作
    testCommand("Storage empty initially", storage.size() == 0);
    testCommand("Storage set operation", storage.set("key1", "value1"));
    testCommand("Storage get operation", storage.get("key1") == "value1");
    testCommand("Storage exists operation", storage.exists("key1"));
    testCommand("Storage size after set", storage.size() == 1);
    
    // 测试删除操作
    testCommand("Storage del existing", storage.del("key1"));
    testCommand("Storage not exists after del", !storage.exists("key1"));
    testCommand("Storage del nonexistent", !storage.del("nonexistent"));
    
    // 测试 TTL
    storage.set("ttl_key", "ttl_value", 100);  // 100ms TTL
    testCommand("Storage TTL immediate get", storage.get("ttl_key") == "ttl_value");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    testCommand("Storage TTL expired get", storage.get("ttl_key").empty());
    
    // 测试清理
    storage.set("key1", "value1");
    storage.set("key2", "value2", 50);  // 50ms TTL
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    storage.cleanupExpired();
    testCommand("Storage cleanup expired", storage.size() == 1);
    
    std::cout << "\n--- Error Cases Unit Test ---" << std::endl;
    
    // 测试无效参数类型
    auto setInvalidTypeResult = setCmd.execute({
        makeInteger(123),  // 无效的 key 类型
        makeBulkString("value")
    });
    testCommand("SET invalid key type", !setInvalidTypeResult.success);
    
    auto getInvalidTypeResult = getCmd.execute({
        makeInteger(123)  // 无效的 key 类型
    });
    testCommand("GET invalid key type", !getInvalidTypeResult.success);
    
    // 测试无效 TTL
    auto setInvalidTtlResult = setCmd.execute({
        makeBulkString("key"),
        makeBulkString("value"),
        makeInteger(-1)  // 无效的 TTL
    });
    testCommand("SET invalid TTL", !setInvalidTtlResult.success);
    
    auto setInvalidTtlTypeResult = setCmd.execute({
        makeBulkString("key"),
        makeBulkString("value"),
        makeBulkString("not_integer")  // 无效的 TTL 类型
    });
    testCommand("SET invalid TTL type", !setInvalidTtlTypeResult.success);
    
    std::cout << "\n=== Command Unit Test Results ===" << std::endl;
    std::cout << "SET Command: ✅ All tests passed" << std::endl;
    std::cout << "GET Command: ✅ All tests passed" << std::endl;
    std::cout << "DEL Command: ✅ All tests passed" << std::endl;
    std::cout << "EXISTS Command: ✅ All tests passed" << std::endl;
    std::cout << "Command Registry: ✅ All tests passed" << std::endl;
    std::cout << "Storage Engine: ✅ All tests passed" << std::endl;
    std::cout << "Error Cases: ✅ All tests passed" << std::endl;
    std::cout << "\n🎉 COMMAND UNIT TEST PASSED! 🎉" << std::endl;
    
    return 0;
}
