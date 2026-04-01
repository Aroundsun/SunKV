#include "FlushAllCommand.h"

ExecuteResult FlushAllCommand::execute(const std::vector<std::string>& args, 
                                       StorageEngine& storage) {
    if (!validateArgs(args)) {
        return ExecuteResult(false, "ERR wrong number of arguments for 'flushall' command");
    }
    
    // 清空所有数据
    storage.clear();
    
    // 返回 OK 响应
    auto response = std::make_unique<RESPValue>(RESPType::SimpleString);
    response->setString("OK");
    
    return ExecuteResult(true, std::move(response));
}

bool FlushAllCommand::validateArgs(const std::vector<std::string>& args) const {
    return args.empty();
}
