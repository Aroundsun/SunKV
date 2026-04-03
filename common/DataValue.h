#pragma once

#include <string>
#include <list>
#include <set>
#include <map>
#include <chrono>

// 数据类型枚举
enum class DataType {
    STRING = 0,
    LIST = 1,
    SET = 2,
    HASH = 3
};

// TTL 相关常量
constexpr int64_t NO_TTL = -1;  // 永不过期
constexpr int64_t TTL_EXPIRED = -2;  // 已过期

// 数据值结构
struct DataValue {
    DataType type;
    std::string string_value;                    // STRING 类型
    std::list<std::string> list_value;          // LIST 类型
    std::set<std::string> set_value;            // SET 类型
    std::map<std::string, std::string> hash_value; // HASH 类型
    
    // TTL 支持
    int64_t ttl_seconds;                         // TTL 秒数，-1 表示永不过期
    std::chrono::steady_clock::time_point created_time;  // 创建时间
    std::chrono::steady_clock::time_point ttl_set_time;   // TTL 设置时间
    
    // 构造函数
    DataValue() : type(DataType::STRING), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::string& val) : type(DataType::STRING), string_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::list<std::string>& val) : type(DataType::LIST), list_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::set<std::string>& val) : type(DataType::SET), set_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::map<std::string, std::string>& val) : type(DataType::HASH), hash_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    
    // 检查是否过期
    bool isExpired() const {
        if (ttl_seconds == NO_TTL) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ttl_set_time);
        return elapsed.count() >= ttl_seconds;
    }
    
    // 获取剩余 TTL（秒）
    int64_t getRemainingTTL() const {
        if (ttl_seconds == NO_TTL) {
            return NO_TTL;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ttl_set_time);
        int64_t remaining = ttl_seconds - elapsed.count();
        return remaining > 0 ? remaining : TTL_EXPIRED;
    }
    
    // 设置 TTL
    void setTTL(int64_t ttl) {
        ttl_seconds = ttl;
        if (ttl > 0) {
            ttl_set_time = std::chrono::steady_clock::now();
        }
    }
};
