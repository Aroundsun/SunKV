#include "KeysCommand.h"
#include <algorithm>

ExecuteResult KeysCommand::execute(const std::vector<std::string>& args, 
                                  StorageEngine& storage) {
    if (!validateArgs(args)) {
        return ExecuteResult(false, "ERR wrong number of arguments for 'keys' command");
    }
    
    const std::string& pattern = args[0];
    
    // 获取所有键
    auto allKeys = storage.getAllKeys();
    std::vector<std::string> matchedKeys;
    
    // 简单的模式匹配实现
    // 支持 * 通配符
    for (const auto& key : allKeys) {
        if (pattern == "*" || key == pattern) {
            matchedKeys.push_back(key);
        }
    }
    
    // 创建 RESP 数组响应
    auto response = std::make_unique<RESPValue>(RESPType::Array);
    for (const auto& key : matchedKeys) {
        auto keyValue = std::make_unique<RESPValue>(RESPType::BulkString);
        keyValue->setString(key);
        response->addArrayElement(std::move(keyValue));
    }
    
    return ExecuteResult(true, std::move(response));
}

bool KeysCommand::validateArgs(const std::vector<std::string>& args) const {
    return args.size() == 1;
}
