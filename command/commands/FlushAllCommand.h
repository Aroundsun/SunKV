#pragma once

#include "../Command.h"

/**
 * @brief FLUSHALL 命令实现
 * 
 * 清空所有数据库中的所有键
 * 语法: FLUSHALL
 */
class FlushAllCommand : public Command {
public:
    /**
     * @brief 执行 FLUSHALL 命令
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
    std::string getName() const override { return "FLUSHALL"; }
    
    /**
     * @brief 获取命令描述
     * @return 命令描述
     */
    std::string getDescription() const override { 
        return "Remove all keys from all databases"; 
    }
    
    /**
     * @brief 获取用法
     * @return 用法说明
     */
    std::string getUsage() const override { 
        return "FLUSHALL"; 
    }
};
