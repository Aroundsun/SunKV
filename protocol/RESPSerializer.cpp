#include "RESPSerializer.h"
#include <sstream>
#include <iomanip>

std::string RESPSerializer::serialize(const RESPValue& value) {
    return serializeValue(value);
}

std::string RESPSerializer::serializeSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string RESPSerializer::serializeError(const std::string& error) {
    return "-" + error + "\r\n";
}

std::string RESPSerializer::serializeInteger(int64_t value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string RESPSerializer::serializeBulkString(const std::string& str) {
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RESPSerializer::serializeNullBulkString() {
    return "$-1\r\n";
}

std::string RESPSerializer::serializeArray(const std::vector<std::unique_ptr<RESPValue>>& array) {
    std::string result = "*" + std::to_string(array.size()) + "\r\n";
    
    for (const auto& item : array) {
        if (item) {
            result += serializeValue(*item);
        } else {
            result += serializeNullBulkString();
        }
    }
    
    return result;
}

std::string RESPSerializer::serializeNullArray() {
    return "*-1\r\n";
}

std::string RESPSerializer::serializeStatus(const std::string& status) {
    return "+" + status + "\r\n";
}

std::string RESPSerializer::serializeValue(const RESPValue& value) {
    switch (value.getType()) {
        case RESPType::SIMPLE_STRING:
            return serializeSimpleString(value.toString());
            
        case RESPType::ERROR:
            return serializeError(value.toString());
            
        case RESPType::INTEGER:
            // 需要从 value 中获取整数值，但 RESPValue 是抽象基类
            // 暂时返回错误
            return serializeError("Integer serialization not implemented");
            
        case RESPType::BULK_STRING:
            if (value.isNull()) {
                return serializeNullBulkString();
            } else {
                return serializeBulkString(value.toString());
            }
            
        case RESPType::ARRAY:
            if (value.isNull()) {
                return serializeNullArray();
            } else {
                // 需要从 value 中获取数组，但 RESPValue 是抽象基类
                // 暂时返回错误
                return serializeError("Array serialization not implemented");
            }
            
        default:
            // 未知类型，返回错误
            return serializeError("Unknown RESP type");
    }
}
