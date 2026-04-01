#include "CommandRegistry.h"
#include "storage/StorageEngine.h"
#include "../protocol/RESPType.h"

CommandRegistry::CommandRegistry() {
    // 暂时不注册任何命令
}

void CommandRegistry::registerCommand(const std::string& name, std::unique_ptr<Command> command) {
    if (command) {
        commands_[name] = std::move(command);
    }
}

Command* CommandRegistry::findCommand(const std::string& name) {
    auto it = commands_.find(name);
    return (it != commands_.end()) ? it->second.get() : nullptr;
}

CommandResult CommandRegistry::executeCommand(const RESPValue& command, StorageEngine& storage) {
    // 暂时返回一个简单的 PONG 响应，不管输入什么
    return CommandResult(true, makeSimpleString("PONG"));
}

std::vector<std::string> CommandRegistry::getAllCommandNames() const {
    std::vector<std::string> names;
    names.reserve(commands_.size());
    
    for (const auto& pair : commands_) {
        names.push_back(pair.first);
    }
    
    return names;
}

size_t CommandRegistry::getCommandCount() const {
    return commands_.size();
}

template<typename CommandType>
void CommandRegistry::registerCommandType(const std::string& name) {
    registerCommand(name, std::make_unique<CommandType>());
}
