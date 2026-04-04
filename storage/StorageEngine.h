#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

// 键值对存储项
struct StorageItem {
    std::string value;
    std::chrono::steady_clock::time_point expire_time;
    bool has_ttl;
    
    StorageItem(const std::string& val) 
        : value(val), has_ttl(false) {}
    
    StorageItem(const std::string& val, int64_t ttl_ms)
        : value(val), 
          expire_time(std::chrono::steady_clock::now() + std::chrono::milliseconds(ttl_ms)),
          has_ttl(true) {}
    
    bool isExpired() const {
        if (!has_ttl) {
            return false;
        }
        return std::chrono::steady_clock::now() > expire_time;
    }
};

// 内存存储引擎
class StorageEngine {
public:
    static StorageEngine& getInstance();
    
    // 设置键值对
    bool set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    // 获取值
    std::string get(const std::string& key);
    
    // 删除键
    bool del(const std::string& key);
    
    // 检查键是否存在
    bool exists(const std::string& key);
    
    // 获取所有键
    std::vector<std::string> keys(const std::string& pattern = "*");
    
    // 清空所有数据
    void clear();
    
    // 获取存储大小
    size_t size();
    
    // 清理过期键
    void cleanupExpired();
    
    // 清理存储引擎（用于优雅关闭）
    void cleanup();

private:
    StorageEngine() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<StorageItem>> data_;
};
