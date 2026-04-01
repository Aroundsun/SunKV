#pragma once

#include "../Command.h"

/**
 * @brief PERSIST 命令实现
 * 
 * 移除键的生存时间，使其永久保存
 * 语法: PERSIST key
 */
class PersistCommand : public Command {
public:
    /**
     * @brief 执行 PERSIST 命令
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
    std::string getName() const override { return "PERSIST"; }
    
    /**
     * @brief 获取命令描述
     * @return 命令描述
     */
    std::string getDescription() const override { 
        return "Remove the expiration from a key"; 
    }
    
    /**
     * @brief 获取用法
     * @return 用法说明
     */
    std::string getUsage() const override { 
        return "PERSIST key"; 
    }
};
