#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include "Command.h"

// 前向声明
class StorageEngine;

/**
 * @brief 命令注册表
 * 
 * 负责管理所有可用的命令，提供命令注册和执行功能
 */
class CommandRegistry {
public:
    /**
     * @brief 构造函数
     */
    CommandRegistry();
    
    /**
     * @brief 析构函数
     */
    ~CommandRegistry() = default;
    
    // 禁用拷贝
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    
    /**
     * @brief 注册命令
     * @param name 命令名称
     * @param command 命令对象
     */
    void registerCommand(const std::string& name, std::unique_ptr<Command> command);
    
    /**
     * @brief 查找命令
     * @param name 命令名称
     * @return 命令对象指针，如果不存在返回 nullptr
     */
    Command* findCommand(const std::string& name);
    
    /**
     * @brief 执行命令
     * @param command RESP 命令对象
     * @param storage 存储引擎
     * @return 执行结果
     */
    CommandResult executeCommand(const RESPValue& command, StorageEngine& storage);
    
    /**
     * @brief 注册所有内置命令
     */
    void registerAllCommands();
    
    /**
     * @brief 获取所有已注册的命令名称
     * @return 命令名称列表
     */
    std::vector<std::string> getAllCommandNames() const;
    
    /**
     * @brief 获取命令数量
     * @return 命令数量
     */
    size_t getCommandCount() const;

private:
    /**
     * @brief 注册单个命令
     */
    template<typename CommandType>
    void registerCommandType(const std::string& name);

private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
};
