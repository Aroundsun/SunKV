#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Command.h"

class CommandRegistry {
public:
    static CommandRegistry& getInstance();

    CommandRegistry();
    ~CommandRegistry() = default;

    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;

    void registerCommand(const std::string& name, CommandCreator creator);
    bool hasCommand(const std::string& name) const;
    std::unique_ptr<Command> createCommand(const std::string& name) const;
    std::vector<std::string> getAllCommands() const;

private:
    void registerBuiltInCommands();
    std::unordered_map<std::string, CommandCreator> creators_;
};

#define REGISTER_COMMAND(cmd_name, cmd_type)                                      \
    namespace {                                                                    \
    struct cmd_type##AutoRegister {                                                \
        cmd_type##AutoRegister() {                                                 \
            CommandRegistry::getInstance().registerCommand(                        \
                (cmd_name), []() { return std::make_unique<cmd_type>(); });        \
        }                                                                          \
    };                                                                             \
    static cmd_type##AutoRegister g_##cmd_type##_auto_register;                    \
    }
