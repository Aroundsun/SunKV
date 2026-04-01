#pragma once

#include "../Command.h"

/**
 * @brief EXPIRE 命令实现
 * 
 * 为键设置生存时间（秒）
 * 语法: EXPIRE key seconds
 */
class ExpireCommand : public Command {
public:
    /**
     * @brief 执行 EXPIRE 命令
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
    std::string getName() const override { return "EXPIRE"; }
    
    /**
     * @brief 获取命令描述
     * @return 命令描述
     */
    std::string getDescription() const override { 
        return "Set a timeout on a key"; 
    }
    
    /**
     * @brief 获取用法
     * @return 用法说明
     */
    std::string getUsage() const override { 
        return "EXPIRE key seconds"; 
    }
};
