#pragma once

#include "../Command.h"
#include "../../storage/StorageEngine.h"

// SET 命令
class SetCommand : public Command {
public:
    std::string name() const override {
        return "set";
    }
    
    std::string description() const override {
        return "Set the string value of a key. Optional TTL in milliseconds can be provided.";
    }
    
    std::pair<int, int> argCount() const override {
        return {2, 3};  // 2-3 个参数 (key value [ttl])
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.size() < 2 || args.size() > 3) {
            return CommandResult::makeError("ERR wrong number of arguments for 'set' command (expected 2-3 arguments)");
        }
        
        // 获取 key
        std::string key;
        if (args[0]->getType() == RESPType::SIMPLE_STRING) {
            key = std::static_pointer_cast<RESPSimpleString>(args[0])->getValue();
        } else if (args[0]->getType() == RESPType::BULK_STRING) {
            key = std::static_pointer_cast<RESPBulkString>(args[0])->getValue();
        } else {
            return CommandResult::makeError("ERR invalid key type");
        }
        
        // 获取 value
        std::string value;
        if (args[1]->getType() == RESPType::SIMPLE_STRING) {
            value = std::static_pointer_cast<RESPSimpleString>(args[1])->getValue();
        } else if (args[1]->getType() == RESPType::BULK_STRING) {
            value = std::static_pointer_cast<RESPBulkString>(args[1])->getValue();
        } else {
            return CommandResult::makeError("ERR invalid value type");
        }
        
        // 解析 TTL（可选）
        int64_t ttl_ms = -1;
        if (args.size() == 3) {
            if (args[2]->getType() == RESPType::INTEGER) {
                ttl_ms = std::static_pointer_cast<RESPInteger>(args[2])->getValue();
                if (ttl_ms < 0) {
                    return CommandResult::makeError("ERR invalid TTL value");
                }
            } else {
                return CommandResult::makeError("ERR TTL must be an integer");
            }
        }
        
        // 执行 SET 操作
        auto& storage = StorageEngine::getInstance();
        bool success = storage.set(key, value, ttl_ms);
        
        if (success) {
            return CommandResult::makeSuccess(makeSimpleString("OK"));
        } else {
            return CommandResult::makeError("ERR failed to set value");
        }
    }
};
