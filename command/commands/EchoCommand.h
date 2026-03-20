#pragma once

#include "../Command.h"

// ECHO 命令
class EchoCommand : public Command {
public:
    std::string name() const override {
        return "echo";
    }
    
    std::string description() const override {
        return "Echo the given string.";
    }
    
    std::pair<int, int> argCount() const override {
        return {1, 1};  // 恰好 1 个参数
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.empty()) {
            return CommandResult::makeError("ERR wrong number of arguments for 'echo' command (expected 1 arguments)");
        }
        
        // 返回第一个参数
        return CommandResult::makeSuccess(args[0]);
    }
};
