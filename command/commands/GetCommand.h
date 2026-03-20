#pragma once

#include "../Command.h"
#include "../../storage/StorageEngine.h"

// GET 命令
class GetCommand : public Command {
public:
    std::string name() const override {
        return "get";
    }
    
    std::string description() const override {
        return "Get the value of a key. Returns (nil) if the key does not exist.";
    }
    
    std::pair<int, int> argCount() const override {
        return {1, 1};  // 恰好 1 个参数
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.size() != 1) {
            return CommandResult::makeError("ERR wrong number of arguments for 'get' command (expected 1 arguments)");
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
        
        // 执行 GET 操作
        auto& storage = StorageEngine::getInstance();
        std::string value = storage.get(key);
        
        if (value.empty()) {
            // 键不存在，返回 nil
            return CommandResult::makeSuccess(makeNullBulkString());
        } else {
            // 返回值
            return CommandResult::makeSuccess(makeBulkString(value));
        }
    }
};
