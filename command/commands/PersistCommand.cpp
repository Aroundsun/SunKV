#include "PersistCommand.h"

ExecuteResult PersistCommand::execute(const std::vector<std::string>& args, 
                                     StorageEngine& storage) {
    if (!validateArgs(args)) {
        return ExecuteResult(false, "ERR wrong number of arguments for 'persist' command");
    }
    
    const std::string& key = args[0];
    
    // 检查键是否存在
    if (!storage.exists(key)) {
        auto response = std::make_unique<RESPValue>(RESPType::Integer);
        response->setInteger(0);  // 键不存在
        return ExecuteResult(true, std::move(response));
    }
    
    // 获取当前 TTL
    int64_t currentTTL = storage.getTTL(key);
    
    // 如果键没有设置过期时间，返回 0
    if (currentTTL == -1) {
        auto response = std::make_unique<RESPValue>(RESPType::Integer);
        response->setInteger(0);
        return ExecuteResult(true, std::move(response));
    }
    
    // 移除过期时间
    bool success = storage.setTTL(key, -1);
    
    auto response = std::make_unique<RESPValue>(RESPType::Integer);
    response->setInteger(success ? 1 : 0);
    
    return ExecuteResult(true, std::move(response));
}

bool PersistCommand::validateArgs(const std::vector<std::string>& args) const {
    return args.size() == 1;
}
