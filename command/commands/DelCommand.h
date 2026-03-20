#pragma once

#include "../Command.h"
#include "../../storage/StorageEngine.h"

// DEL 命令
class DelCommand : public Command {
public:
    std::string name() const override {
        return "del";
    }
    
    std::string description() const override {
        return "Remove the specified keys. Returns the number of keys removed.";
    }
    
    std::pair<int, int> argCount() const override {
        return {1, -1};  // 至少 1 个参数，无上限
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.empty()) {
            return CommandResult::makeError("ERR wrong number of arguments for 'del' command (expected at least 1 arguments)");
        }
        
        auto& storage = StorageEngine::getInstance();
        int deleted_count = 0;
        
        for (const auto& arg : args) {
            std::string key;
            
            if (arg->getType() == RESPType::SIMPLE_STRING) {
                key = std::static_pointer_cast<RESPSimpleString>(arg)->getValue();
            } else if (arg->getType() == RESPType::BULK_STRING) {
                key = std::static_pointer_cast<RESPBulkString>(arg)->getValue();
            } else {
                return CommandResult::makeError("ERR invalid key type");
            }
            
            // 执行 DEL 操作
            if (storage.del(key)) {
                deleted_count++;
            }
        }
        
        // 返回删除的键数量
        return CommandResult::makeSuccess(makeInteger(deleted_count));
    }
};
