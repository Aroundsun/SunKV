#pragma once

#include "../Command.h"

// PING 命令
class PingCommand : public Command {
public:
    std::string name() const override {
        return "ping";
    }
    
    std::string description() const override {
        return "Ping the server. Returns PONG if no argument is provided, otherwise returns the argument.";
    }
    
    std::pair<int, int> argCount() const override {
        return {0, 1};  // 0-1 个参数
    }
    
    CommandResult execute(const std::vector<RESPValue::Ptr>& args) override {
        if (args.empty()) {
            return CommandResult::makeSuccess(makeSimpleString("PONG"));
        } else {
            // 返回第一个参数
            return CommandResult::makeSuccess(args[0]);
        }
    }
};
