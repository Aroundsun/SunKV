#pragma once

#include <string>
#include <vector>
#include <memory>

// RESP 协议数据类型枚举
enum class RESPType {
    SIMPLE_STRING = '+',  // 简单字符串
    ERROR = '-',           // 错误信息
    INTEGER = ':',        // 整数
    BULK_STRING = '$',    // 批量字符串
    ARRAY = '*',          // 数组
    NULL_BULK = '$',     // 空批量字符串
    NULL_ARRAY = '*'      // 空数组
};

// RESP 协议值基类
class RESPValue {
public:
    using Ptr = std::shared_ptr<RESPValue>;
    
    virtual ~RESPValue() = default;
    
    virtual RESPType getType() const = 0;
    virtual std::string encode() const = 0;
    virtual std::string toString() const = 0;
    
    // 类型检查方法
    bool isSimpleString() const { return getType() == RESPType::SIMPLE_STRING; }
    bool isError() const { return getType() == RESPType::ERROR; }
    bool isInteger() const { return getType() == RESPType::INTEGER; }
    bool isBulkString() const { return getType() == RESPType::BULK_STRING; }
    bool isArray() const { return getType() == RESPType::ARRAY; }
    bool isNull() const { return getType() == RESPType::NULL_BULK || getType() == RESPType::NULL_ARRAY; }
};

// 简单字符串
class RESPSimpleString : public RESPValue {
public:
    explicit RESPSimpleString(const std::string& value) : value_(value) {}
    
    RESPType getType() const override { return RESPType::SIMPLE_STRING; }
    std::string encode() const override;
    std::string toString() const override { return value_; }
    
    const std::string& getValue() const { return value_; }
    void setValue(const std::string& value) { value_ = value; }
    
private:
    std::string value_;
};

// 错误信息
class RESPError : public RESPValue {
public:
    explicit RESPError(const std::string& message) : message_(message) {}
    
    RESPType getType() const override { return RESPType::ERROR; }
    std::string encode() const override;
    std::string toString() const override { return "ERROR: " + message_; }
    
    const std::string& getMessage() const { return message_; }
    void setMessage(const std::string& message) { message_ = message; }
    
private:
    std::string message_;
};

// 整数
class RESPInteger : public RESPValue {
public:
    explicit RESPInteger(int64_t value) : value_(value) {}
    
    RESPType getType() const override { return RESPType::INTEGER; }
    std::string encode() const override;
    std::string toString() const override { return std::to_string(value_); }
    
    int64_t getValue() const { return value_; }
    void setValue(int64_t value) { value_ = value; }
    
private:
    int64_t value_;
};

// 批量字符串
class RESPBulkString : public RESPValue {
public:
    explicit RESPBulkString(const std::string& value) : value_(value) {}
    explicit RESPBulkString() : null_(true) {}  // 空批量字符串
    
    RESPType getType() const override { return null_ ? RESPType::NULL_BULK : RESPType::BULK_STRING; }
    std::string encode() const override;
    std::string toString() const override { return null_ ? "(null)" : value_; }
    
    const std::string& getValue() const { return value_; }
    void setValue(const std::string& value) { value_ = value; null_ = false; }
    bool isNull() const { return null_; }
    
private:
    std::string value_;
    bool null_ = false;
};

// 数组
class RESPArray : public RESPValue {
public:
    explicit RESPArray(const std::vector<RESPValue::Ptr>& values) : values_(values) {}
    explicit RESPArray() : null_(true) {}  // 空数组
    
    RESPType getType() const override { return null_ ? RESPType::NULL_ARRAY : RESPType::ARRAY; }
    std::string encode() const override;
    std::string toString() const override;
    
    const std::vector<RESPValue::Ptr>& getValues() const { return values_; }
    void setValues(const std::vector<RESPValue::Ptr>& values) { values_ = values; null_ = false; }
    void addValue(RESPValue::Ptr value) { values_.push_back(value); null_ = false; }
    bool isNull() const { return null_; }
    size_t size() const { return values_.size(); }
    
private:
    std::vector<RESPValue::Ptr> values_;
    bool null_ = false;
};

// 工厂函数
inline RESPValue::Ptr makeSimpleString(const std::string& value) {
    return std::make_shared<RESPSimpleString>(value);
}

inline RESPValue::Ptr makeError(const std::string& message) {
    return std::make_shared<RESPError>(message);
}

inline RESPValue::Ptr makeInteger(int64_t value) {
    return std::make_shared<RESPInteger>(value);
}

inline RESPValue::Ptr makeBulkString(const std::string& value) {
    return std::make_shared<RESPBulkString>(value);
}

inline RESPValue::Ptr makeNullBulkString() {
    return std::make_shared<RESPBulkString>();
}

inline RESPValue::Ptr makeArray(const std::vector<RESPValue::Ptr>& values) {
    return std::make_shared<RESPArray>(values);
}

inline RESPValue::Ptr makeNullArray() {
    return std::make_shared<RESPArray>();
}
