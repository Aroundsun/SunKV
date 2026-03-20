#include "Command.h"
#include <algorithm>

// CommandRegistry 实现
CommandRegistry& CommandRegistry::getInstance() {
    static CommandRegistry instance;
    return instance;
}

void CommandRegistry::registerCommand(const std::string& name, CommandCreator creator) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    commands_[lowerName] = creator;
}

std::unique_ptr<Command> CommandRegistry::createCommand(const std::string& name) const {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    auto it = commands_.find(lowerName);
    if (it != commands_.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> CommandRegistry::getAllCommands() const {
    std::vector<std::string> result;
    result.reserve(commands_.size());
    
    for (const auto& pair : commands_) {
        result.push_back(pair.first);
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

bool CommandRegistry::hasCommand(const std::string& name) const {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    return commands_.find(lowerName) != commands_.end();
}
