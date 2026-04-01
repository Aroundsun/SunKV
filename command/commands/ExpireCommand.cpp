#include "ExpireCommand.h"

ExecuteResult ExpireCommand::execute(const std::vector<std::string>& args, 
                                   StorageEngine& storage) {
    if (!validateArgs(args)) {
        return ExecuteResult(false, "ERR wrong number of arguments for 'expire' command");
    }
    
    const std::string& key = args[0];
    
    // 解析 TTL 值
    int64_t ttl;
    try {
        ttl = std::stoll(args[1]);
    } catch (const std::exception& e) {
        return ExecuteResult(false, "ERR value is not an integer or out of range");
    }
    
    if (ttl < 0) {
        return ExecuteResult(false, "ERR invalid expire time");
    }
    
    // 检查键是否存在
    if (!storage.exists(key)) {
        auto response = std::make_unique<RESPValue>(RESPType::Integer);
        response->setInteger(0);  // 键不存在，设置失败
        return ExecuteResult(true, std::move(response));
    }
    
    // 设置 TTL
    bool success = storage.setTTL(key, ttl);
    
    auto response = std::make_unique<RESPValue>(RESPType::Integer);
    response->setInteger(success ? 1 : 0);
    
    return ExecuteResult(true, std::move(response));
}

bool ExpireCommand::validateArgs(const std::vector<std::string>& args) const {
    if (args.size() != 2) {
        return false;
    }
    
    // 验证 TTL 参数是否为数字
    try {
        std::stoll(args[1]);
        return true;
    } catch (...) {
        return false;
    }
}
