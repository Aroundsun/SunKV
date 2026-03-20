#pragma once

#include "../Command.h"
#include "../../storage/StorageEngine.h"

// EXISTS 命令
class ExistsCommand : public Command {
public:
    std::string name() const override {
        return "exists";
    }
    
    std::string description() const override {
        return "Determine if a key exists. Returns 1 if the key exists, 0 otherwise.";
    }
    
    std::pair<int, int> argCount() const override {
        return {1, 1};  // 恰好 1 个参数
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.size() != 1) {
            return CommandResult::makeError("ERR wrong number of arguments for 'exists' command (expected 1 arguments)");
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
        
        // 执行 EXISTS 操作
        auto& storage = StorageEngine::getInstance();
        bool exists = storage.exists(key);
        
        // 返回 1 或 0
        return CommandResult::makeSuccess(makeInteger(exists ? 1 : 0));
    }
};
