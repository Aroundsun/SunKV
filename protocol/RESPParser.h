#pragma once

#include "RESPType.h"
#include <string>
#include <memory>

// RESP 解析状态
enum class ParseState {
    START,             // 开始解析
    SIMPLE_STRING,      // 简单字符串
    ERROR,              // 错误信息
    INTEGER,            // 整数
    BULK_STRING_SIZE,   // 批量字符串长度
    BULK_STRING_DATA,   // 批量字符串数据
    ARRAY_SIZE,         // 数组长度
    ARRAY_ELEMENT        // 数组元素
};

// RESP 解析结果
struct ParseResult {
    bool success;
    bool complete;
    size_t processed_bytes;  // 已处理的字节数
    RESPValue::Ptr value;
    std::string error;
    
    static ParseResult makeSuccess(RESPValue::Ptr value, size_t processed) {
        return {true, true, processed, value, ""};
    }
    
    static ParseResult makeIncomplete(size_t processed) {
        return {true, false, processed, nullptr, ""};
    }
    
    static ParseResult makeError(const std::string& message) {
        return {false, false, 0, nullptr, message};
    }
};

// RESP 协议解析器
class RESPParser {
public:
    RESPParser();
    ~RESPParser() = default;
    
    // 解析数据
    ParseResult parse(const std::string& data);
    
    // 重置解析器状态
    void reset();
    
    // 获取已解析的字节数
    size_t getProcessedBytes() const { return processed_bytes_; }
    
    // 检查是否正在解析数组
    bool isInArray() const { return array_stack_.size() > 0; }
    
    // 获取当前解析深度
    size_t getDepth() const { return array_stack_.size(); }

private:
    // 解析不同类型的方法
    ParseResult parseSimpleString(const std::string& data, size_t& pos);
    ParseResult parseError(const std::string& data, size_t& pos);
    ParseResult parseInteger(const std::string& data, size_t& pos);
    ParseResult parseBulkStringSize(const std::string& data, size_t& pos);
    ParseResult parseBulkStringData(const std::string& data, size_t& pos);
    ParseResult parseArraySize(const std::string& data, size_t& pos);
    ParseResult parseArrayElement(const std::string& data, size_t& pos);
    
    // 辅助方法
    bool findCRLF(const std::string& data, size_t pos, size_t& crlf_pos);
    int64_t parseInteger(const std::string& data, size_t start, size_t end);
    
    // 数组栈管理
    struct ArrayContext {
        size_t size;
        size_t count;
        std::vector<RESPValue::Ptr> elements;
    };
    
    std::vector<ArrayContext> array_stack_;
    
    // 解析状态
    ParseState state_;
    
    // 临时数据
    std::string temp_data_;
    int64_t bulk_size_;
    size_t processed_bytes_;
    
    // 当前解析的值
    RESPValue::Ptr current_value_;
};
