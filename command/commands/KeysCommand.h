#pragma once

#include "../Command.h"

/**
 * @brief KEYS 命令实现
 * 
 * 返回匹配模式的所有键名
 * 语法: KEYS pattern
 */
class KeysCommand : public Command {
public:
    /**
     * @brief 执行 KEYS 命令
     * @param args 命令参数
     * @param storage 存储引擎
     * @return 执行结果
     */
    ExecuteResult execute(const std::vector<std::string>& args, 
                         StorageEngine& storage) override;
    
    /**
     * @brief 验证参数
     * @param args 命令参数
     * @return 是否有效
     */
    bool validateArgs(const std::vector<std::string>& args) const override;
    
    /**
     * @brief 获取命令名称
     * @return 命令名称
     */
    std::string getName() const override { return "KEYS"; }
    
    /**
     * @brief 获取命令描述
     * @return 命令描述
     */
    std::string getDescription() const override { 
        return "Find all keys matching a pattern"; 
    }
    
    /**
     * @brief 获取用法
     * @return 用法说明
     */
    std::string getUsage() const override { 
        return "KEYS pattern"; 
    }
};
