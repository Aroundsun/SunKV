#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "protocol/RESPType.h"

// 命令执行结果
struct CommandResult {
    bool success;
    RESPValue::Ptr response;
    std::string error;
    
    static CommandResult makeSuccess(RESPValue::Ptr response) {
        return {true, response, ""};
    }
    
    static CommandResult makeError(const std::string& error) {
        return {false, nullptr, error};
    }
};

// 命令基类
class Command {
public:
    virtual ~Command() = default;
    
    // 获取命令名称
    virtual std::string name() const = 0;
    
    // 获取命令描述
    virtual std::string description() const = 0;
    
    // 获取参数数量范围 {min, max}
    virtual std::pair<int, int> argCount() const = 0;
    
    // 执行命令
    virtual CommandResult execute(const std::vector<RESPValue::Ptr>& args) = 0;
    
    // 验证参数数量
    bool validateArgs(const std::vector<RESPValue::Ptr>& args) const {
        auto [min, max] = argCount();
        int argSize = static_cast<int>(args.size());
        return argSize >= min && (max == -1 || argSize <= max);
    }
    
    // 获取参数数量错误信息
    std::string getArgCountError(int /*actualCount*/) const {
        auto [min, max] = argCount();
        std::string error = "ERR wrong number of arguments for '" + name() + "' command";
        
        if (min == max) {
            error += " (expected " + std::to_string(min) + " arguments)";
        } else if (max == -1) {
            error += " (expected at least " + std::to_string(min) + " arguments)";
        } else {
            error += " (expected " + std::to_string(min) + " to " + std::to_string(max) + " arguments)";
        }
        
        return error;
    }
};

// 命令工厂类型
using CommandCreator = std::function<std::unique_ptr<Command>()>;

#endif // COMMAND_H
