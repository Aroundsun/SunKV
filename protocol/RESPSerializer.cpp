#include "RESPSerializer.h"

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
    // RESPValue 的 encode() 已经覆盖了所有派生类型的正确编码逻辑，
    // 这里统一复用 encode()，避免在抽象基类上做类型向下转型遗漏。
    return value.encode();
}
