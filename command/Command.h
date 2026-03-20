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

// 命令注册器
class CommandRegistry {
public:
    static CommandRegistry& getInstance();
    
    // 注册命令
    void registerCommand(const std::string& name, CommandCreator creator);
    
    // 创建命令
    std::unique_ptr<Command> createCommand(const std::string& name) const;
    
    // 获取所有已注册的命令名称
    std::vector<std::string> getAllCommands() const;
    
    // 检查命令是否存在
    bool hasCommand(const std::string& name) const;

private:
    CommandRegistry() = default;
    std::unordered_map<std::string, CommandCreator> commands_;
};

// 命令注册宏
#define REGISTER_COMMAND(name, className) \
    namespace { \
        struct className##Registrar { \
            className##Registrar() { \
                CommandRegistry::getInstance().registerCommand(name, []() { \
                    return std::make_unique<className>(); \
                }); \
            } \
        }; \
        static className##Registrar g_##className##Registrar; \
    }
