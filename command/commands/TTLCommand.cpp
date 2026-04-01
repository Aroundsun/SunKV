#include "TTLCommand.h"

ExecuteResult TTLCommand::execute(const std::vector<std::string>& args, 
                                 StorageEngine& storage) {
    if (!validateArgs(args)) {
        return ExecuteResult(false, "ERR wrong number of arguments for 'ttl' command");
    }
    
    const std::string& key = args[0];
    
    // 检查键是否存在
    if (!storage.exists(key)) {
        auto response = std::make_unique<RESPValue>(RESPType::Integer);
        response->setInteger(-2);  // 键不存在
        return ExecuteResult(true, std::move(response));
    }
    
    // 获取 TTL
    int64_t ttl = storage.getTTL(key);
    
    auto response = std::make_unique<RESPValue>(RESPType::Integer);
    response->setInteger(ttl);
    
    return ExecuteResult(true, std::move(response));
}

bool TTLCommand::validateArgs(const std::vector<std::string>& args) const {
    return args.size() == 1;
}
