#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "protocol/RESPType.h"

// 命令执行结果
struct CommandResult {
    bool success;
    RESPValue::Ptr response;
    std::string error;

    static CommandResult makeSuccess(RESPValue::Ptr response_value) {
        return {true, std::move(response_value), ""};
    }

    static CommandResult makeError(const std::string& error_message) {
        return {false, nullptr, error_message};
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

    // 获取参数数量范围 {min, max}; max=-1 表示无上限
    virtual std::pair<int, int> argCount() const = 0;

    // 执行命令
    virtual CommandResult execute(const std::vector<RESPValue::Ptr>& args) = 0;

    // 验证参数数量
    bool validateArgs(const std::vector<RESPValue::Ptr>& args) const {
        auto [min, max] = argCount();
        int arg_size = static_cast<int>(args.size());
        return arg_size >= min && (max == -1 || arg_size <= max);
    }

    // 获取参数数量错误信息
    std::string getArgCountError(int /*actual_count*/) const {
        auto [min, max] = argCount();
        std::string msg = "ERR wrong number of arguments for '" + name() + "' command";
        if (min == max) {
            msg += " (expected " + std::to_string(min) + " arguments)";
        } else if (max == -1) {
            msg += " (expected at least " + std::to_string(min) + " arguments)";
        } else {
            msg += " (expected " + std::to_string(min) + " to " + std::to_string(max) + " arguments)";
        }
        return msg;
    }
};

// 命令工厂类型
using CommandCreator = std::function<std::unique_ptr<Command>()>;
