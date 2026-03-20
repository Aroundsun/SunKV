#include "RESPType.h"
#include <sstream>

// 简单字符串编码: +message\r\n
std::string RESPSimpleString::encode() const {
    return "+" + value_ + "\r\n";
}

// 错误信息编码: -error-message\r\n
std::string RESPError::encode() const {
    return "-" + message_ + "\r\n";
}

// 整数编码: :number\r\n
std::string RESPInteger::encode() const {
    return ":" + std::to_string(value_) + "\r\n";
}

// 批量字符串编码: $length\r\ndata\r\n 或 $-1\r\n (null)
std::string RESPBulkString::encode() const {
    if (null_) {
        return "$-1\r\n";
    }
    return "$" + std::to_string(value_.length()) + "\r\n" + value_ + "\r\n";
}

// 数组编码: *length\r\n...elements... 或 *-1\r\n (null)
std::string RESPArray::encode() const {
    if (null_) {
        return "*-1\r\n";
    }
    
    std::ostringstream oss;
    oss << "*" << values_.size() << "\r\n";
    
    for (const auto& value : values_) {
        oss << value->encode();
    }
    
    return oss.str();
}

// 数组转换为字符串（用于调试）
std::string RESPArray::toString() const {
    if (null_) {
        return "(null array)";
    }
    
    std::ostringstream oss;
    oss << "[";
    
    for (size_t i = 0; i < values_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << values_[i]->toString();
    }
    
    oss << "]";
    return oss.str();
}
