#include "CommandRegistry.h"
#include <algorithm>
#include "commands/DelCommand.h"
#include "commands/EchoCommand.h"
#include "commands/ExistsCommand.h"
#include "commands/GetCommand.h"
#include "commands/PingCommand.h"
#include "commands/SetCommand.h"

CommandRegistry& CommandRegistry::getInstance() {
    static CommandRegistry instance;
    return instance;
}

CommandRegistry::CommandRegistry() {
    registerBuiltInCommands();
}

void CommandRegistry::registerCommand(const std::string& name, CommandCreator creator) {
    if (name.empty() || !creator) {
        return;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    creators_[lower] = std::move(creator);
}

bool CommandRegistry::hasCommand(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return creators_.find(lower) != creators_.end();
}

std::unique_ptr<Command> CommandRegistry::createCommand(const std::string& name) const {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto it = creators_.find(lower);
    if (it == creators_.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> CommandRegistry::getAllCommands() const {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& pair : creators_) {
        names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void CommandRegistry::registerBuiltInCommands() {
    registerCommand("ping", []() { return std::make_unique<PingCommand>(); });
    registerCommand("echo", []() { return std::make_unique<EchoCommand>(); });
    registerCommand("set", []() { return std::make_unique<SetCommand>(); });
    registerCommand("get", []() { return std::make_unique<GetCommand>(); });
    registerCommand("del", []() { return std::make_unique<DelCommand>(); });
    registerCommand("exists", []() { return std::make_unique<ExistsCommand>(); });
}
