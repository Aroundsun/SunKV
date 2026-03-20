#pragma once

#include "Command.h"
#include <unordered_map>

// 命令分发器
class CommandDispatcher {
public:
    explicit CommandDispatcher();
    ~CommandDispatcher() = default;
    
    // 分发命令
    CommandResult dispatch(const std::string& commandName, const std::vector<RESPValue::Ptr>& args);
    
    // 处理 RESP 数组命令
    CommandResult handleCommand(RESPValue::Ptr command);
    
    // 获取帮助信息
    RESPValue::Ptr getHelp(const std::string& commandName = "");
    
    // 获取所有命令列表
    RESPValue::Ptr getCommandList();

private:
    // 解析命令名称和参数
    std::pair<std::string, std::vector<RESPValue::Ptr>> parseCommand(RESPValue::Ptr command);
    
    // 验证命令格式
    bool validateCommand(RESPValue::Ptr command);
    
    CommandRegistry& registry_;
};
