#include "StorageEngine.h"
#include "network/logger.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <glob.h>

StorageEngine& StorageEngine::getInstance() {
    static StorageEngine instance;
    return instance;
}

bool StorageEngine::set(const std::string& key, const std::string& value, int64_t ttl_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ttl_ms > 0) {
        data_[key] = std::make_shared<StorageItem>(value, ttl_ms);
    } else {
        data_[key] = std::make_shared<StorageItem>(value);
    }
    
    return true;
}

std::string StorageEngine::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(key);
    if (it == data_.end()) {
        return "";
    }
    
    // 检查是否过期
    if (it->second->isExpired()) {
        data_.erase(it);
        return "";
    }
    
    return it->second->value;
}

bool StorageEngine::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }
    
    data_.erase(it);
    return true;
}

bool StorageEngine::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }
    
    // 检查是否过期
    if (it->second->isExpired()) {
        data_.erase(it);
        return false;
    }
    
    return true;
}

std::vector<std::string> StorageEngine::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    // 简单的通配符匹配实现
    for (const auto& pair : data_) {
        if (pair.second->isExpired()) {
            continue; // 跳过过期的键
        }
        
        if (pattern == "*" || pattern == pair.first) {
            result.push_back(pair.first);
        }
    }
    
    std::sort(result.begin(), result.end());
    return result;
}

void StorageEngine::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
}

size_t StorageEngine::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& pair : data_) {
        if (!pair.second->isExpired()) {
            count++;
        }
    }
    
    return count;
}

void StorageEngine::cleanupExpired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.begin();
    while (it != data_.end()) {
        if (it->second->isExpired()) {
            it = data_.erase(it);
        } else {
            ++it;
        }
    }
}

void StorageEngine::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 清理过期键 - 直接调用清理逻辑，避免死锁
    auto it = data_.begin();
    while (it != data_.end()) {
        if (it->second->isExpired()) {
            it = data_.erase(it);
        } else {
            ++it;
        }
    }
    
    // 清空所有数据
    data_.clear();
    
    LOG_INFO("Storage engine cleanup completed");
}
