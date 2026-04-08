#include "CommandDispatcher.h"
#include "network/logger.h"
#include <algorithm>

CommandDispatcher::CommandDispatcher() : registry_(CommandRegistry::getInstance()) {
}

CommandResult CommandDispatcher::dispatch(const std::string& commandName, const std::vector<RESPValue::Ptr>& args) {
    // 检查命令是否存在
    if (!registry_.hasCommand(commandName)) {
        return CommandResult::makeError("ERR unknown command '" + commandName + "'");
    }
    
    // 创建命令实例
    auto command = registry_.createCommand(commandName);
    if (!command) {
        return CommandResult::makeError("ERR failed to create command '" + commandName + "'");
    }
    
    // 验证参数数量
    if (!command->validateArgs(args)) {
        return CommandResult::makeError(command->getArgCountError(static_cast<int>(args.size())));
    }
    
    try {
        // 执行命令
        return command->execute(args);
    } catch (const std::exception& e) {
        LOG_ERROR("命令执行错误: {}", e.what());
        return CommandResult::makeError("ERR internal server error");
    }
}

CommandResult CommandDispatcher::handleCommand(RESPValue::Ptr command) {
    // 验证命令格式
    if (!validateCommand(command)) {
        return CommandResult::makeError("ERR command must be an array");
    }
    
    // 解析命令名称和参数
    auto [commandName, args] = parseCommand(command);
    
    // 分发命令
    return dispatch(commandName, args);
}

std::pair<std::string, std::vector<RESPValue::Ptr>> CommandDispatcher::parseCommand(RESPValue::Ptr command) {
    std::string commandName;
    std::vector<RESPValue::Ptr> args;
    
    if (command->getType() == RESPType::ARRAY) {
        auto array = std::static_pointer_cast<RESPArray>(command);
        const auto& elements = array->getValues();
        
        if (!elements.empty()) {
            // 第一个元素是命令名称
            auto firstElement = elements[0];
            if (firstElement->getType() == RESPType::SIMPLE_STRING || 
                firstElement->getType() == RESPType::BULK_STRING) {
                
                if (firstElement->getType() == RESPType::SIMPLE_STRING) {
                    commandName = std::static_pointer_cast<RESPSimpleString>(firstElement)->getValue();
                } else {
                    commandName = std::static_pointer_cast<RESPBulkString>(firstElement)->getValue();
                }
                
                // 转换为小写
                std::transform(commandName.begin(), commandName.end(), commandName.begin(), ::tolower);
            }
            
            // 其余元素是参数
            for (size_t i = 1; i < elements.size(); ++i) {
                args.push_back(elements[i]);
            }
        }
    }
    
    return {commandName, args};
}

bool CommandDispatcher::validateCommand(RESPValue::Ptr command) {
    if (!command) {
        return false;
    }
    
    return command->getType() == RESPType::ARRAY;
}

RESPValue::Ptr CommandDispatcher::getHelp(const std::string& commandName) {
    if (commandName.empty()) {
        // 返回所有命令列表
        auto commands = registry_.getAllCommands();
        std::vector<RESPValue::Ptr> commandElements;
        
        for (const auto& name : commands) {
            commandElements.push_back(makeBulkString(name));
        }
        
        return makeArray(commandElements);
    }
    
    // 返回特定命令的帮助信息
    if (!registry_.hasCommand(commandName)) {
        return makeError("ERR unknown command '" + commandName + "'");
    }
    
    auto command = registry_.createCommand(commandName);
    if (!command) {
        return makeError("ERR failed to create command '" + commandName + "'");
    }
    
    std::string help = command->name() + " - " + command->description();
    auto [min, max] = command->argCount();
    help += "\nArguments: ";
    
    if (min == max) {
        help += std::to_string(min);
    } else if (max == -1) {
        help += std::to_string(min) + "+";
    } else {
        help += std::to_string(min) + "-" + std::to_string(max);
    }
    
    return makeBulkString(help);
}

RESPValue::Ptr CommandDispatcher::getCommandList() {
    auto commands = registry_.getAllCommands();
    std::vector<RESPValue::Ptr> commandElements;
    
    for (const auto& name : commands) {
        commandElements.push_back(makeBulkString(name));
    }
    
    return makeArray(commandElements);
}
