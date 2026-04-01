#pragma once

#include "Command.h"
#include "../protocol/RESPType.h"

class SimplePingCommand : public Command {
public:
    CommandResult execute(const std::vector<std::string>& args, 
                     StorageEngine& storage) override {
        // 简单的 PING 命令实现
        return CommandResult(true, makeSimpleString("PONG"));
    }
    
    std::string getName() const override {
        return "ping";
    }
    
    std::string getDescription() const override {
        return "Ping the server";
    }
    
    std::string getUsage() const override {
        return "PING";
    }
    
    bool validateArgs(const std::vector<std::string>& args) const override {
        return true; // PING 不需要参数
    }
};
