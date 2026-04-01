#ifndef COMMAND_H
#define COMMAND_H

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

// 前向声明
class StorageEngine;

// 命令执行结果
struct CommandResult {
    bool success;
    std::string message;
    std::shared_ptr<class RESPValue> response;
    
    CommandResult(bool s, const std::string& msg) 
        : success(s), message(msg) {}
    
    CommandResult(bool s, std::shared_ptr<class RESPValue> resp)
        : success(s), response(std::move(resp)) {}
};

// 命令基类
class Command {
public:
    virtual ~Command() = default;
    
    // 执行命令
    virtual CommandResult execute(const std::vector<std::string>& args, 
                             StorageEngine& storage) = 0;
    
    // 验证参数
    virtual bool validateArgs(const std::vector<std::string>& args) const = 0;
    
    // 获取命令名称
    virtual std::string getName() const = 0;
    
    // 获取命令描述
    virtual std::string getDescription() const = 0;
    
    // 获取用法
    virtual std::string getUsage() const = 0;
    
protected:
    // 验证参数数量
    std::string validateArgCount(const std::vector<std::string>& args, 
                               size_t min, size_t max = SIZE_MAX) const {
        if (args.size() < min || args.size() > max) {
            std::string error = "ERR wrong number of arguments for '" + getName() + "' command";
            if (max != SIZE_MAX) {
                error += " (expected " + std::to_string(min) + " to " + std::to_string(max) + " arguments)";
            } else {
                error += " (expected at least " + std::to_string(min) + " arguments)";
            }
            return error;
        }
        
        return "";
    }
};

// 命令工厂类型
using CommandCreator = std::function<std::unique_ptr<Command>()>;

#endif // COMMAND_H
