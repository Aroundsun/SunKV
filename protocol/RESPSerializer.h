#pragma once

#include <string>
#include <vector>
#include <memory>
#include "RESPType.h"

/**
 * @brief RESP 协议序列化器
 * 
 * 负责将 RESPValue 对象序列化为 RESP 协议格式的字符串
 */
class RESPSerializer {
public:
    /**
     * @brief 序列化 RESPValue
     * @param value 要序列化的值
     * @return 序列化后的字符串
     */
    static std::string serialize(const RESPValue& value);
    
    /**
     * @brief 序列化简单字符串
     * @param str 字符串内容
     * @return 序列化后的字符串
     */
    static std::string serializeSimpleString(const std::string& str);
    
    /**
     * @brief 序列化错误信息
     * @param error 错误信息
     * @return 序列化后的字符串
     */
    static std::string serializeError(const std::string& error);
    
    /**
     * @brief 序列化整数
     * @param value 整数值
     * @return 序列化后的字符串
     */
    static std::string serializeInteger(int64_t value);
    
    /**
     * @brief 序列化批量字符串
     * @param str 字符串内容
     * @return 序列化后的字符串
     */
    static std::string serializeBulkString(const std::string& str);
    
    /**
     * @brief 序列化空批量字符串
     * @return 序列化后的字符串
     */
    static std::string serializeNullBulkString();
    
    /**
     * @brief 序列化数组
     * @param array 数组内容
     * @return 序列化后的字符串
     */
    static std::string serializeArray(const std::vector<std::unique_ptr<RESPValue>>& array);
    
    /**
     * @brief 序列化空数组
     * @return 序列化后的字符串
     */
    static std::string serializeNullArray();
    
    /**
     * @brief 序列化状态回复
     * @param status 状态字符串
     * @return 序列化后的字符串
     */
    static std::string serializeStatus(const std::string& status);

private:
    /**
     * @brief 序列化单个 RESPValue（内部使用）
     * @param value 要序列化的值
     * @return 序列化后的字符串
     */
    static std::string serializeValue(const RESPValue& value);
};
