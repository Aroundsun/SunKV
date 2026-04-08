/**
 * @file StorageEngine.h
 * @brief SunKV 的内存存储引擎定义
 * 
 * 该文件定义了核心内存存储能力，主要包括：
 * - 线程安全的键值存储
 * - TTL（生存时间）能力
 * - 基于模式的键匹配
 * - 过期键自动清理
 * - 通过单例模式提供全局访问入口
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

/**
 * @struct StorageItem
 * @brief 存储项结构，包含值与可选过期信息
 * 
 * 该结构封装了一个值，以及对应的过期时间和 TTL 状态。
 */
struct StorageItem {
    std::string value;                                       ///< 实际存储的字符串值
    std::chrono::steady_clock::time_point expire_time;      ///< 过期时间点（steady_clock）
    bool has_ttl;                                            ///< 是否设置了 TTL
    
    /**
     * @brief 构造一个不带 TTL 的存储项
     * @param val 要存储的值
     */
    StorageItem(const std::string& val) 
        : value(val), has_ttl(false) {}
    
    /**
     * @brief 构造一个带 TTL 的存储项
     * @param val 要存储的值
     * @param ttl_ms 生存时间，单位毫秒
     */
    StorageItem(const std::string& val, int64_t ttl_ms)
        : value(val), 
          expire_time(std::chrono::steady_clock::now() + std::chrono::milliseconds(ttl_ms)),
          has_ttl(true) {}
    
    /**
     * @brief 判断当前存储项是否已过期
     * @return 已过期返回 true，否则返回 false
     */
    bool isExpired() const {
        if (!has_ttl) {
            return false;
        }
        return std::chrono::steady_clock::now() > expire_time;
    }
};

/**
 * @class StorageEngine
 * @brief 线程安全的内存存储引擎
 * 
 * 该类提供线程安全的键值存储能力，支持 TTL 与模式匹配。
 * 采用单例模式，便于全局统一访问。
 */
class StorageEngine {
public:
    /**
     * @brief 获取存储引擎单例实例
     * @return StorageEngine 实例引用
     */
    static StorageEngine& getInstance();
    
    /**
     * @brief 设置键值对，并可选设置 TTL
     * @param key 要写入的键
     * @param value 要写入的值
     * @param ttl_ms 生存时间（毫秒），-1 表示不过期
     * @return 成功返回 true，失败返回 false
     */
    bool set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    /**
     * @brief 获取指定键对应的值
     * @param key 要查询的键
     * @return 若存在则返回值，否则返回空字符串
     */
    std::string get(const std::string& key);
    
    /**
     * @brief 删除指定键
     * @param key 要删除的键
     * @return 删除成功返回 true，未找到返回 false
     */
    bool del(const std::string& key);
    
    /**
     * @brief 判断指定键是否存在
     * @param key 要检查的键
     * @return 存在返回 true，否则返回 false
     */
    bool exists(const std::string& key);
    
    /**
     * @brief 获取所有匹配模式的键
     * @param pattern 匹配模式（支持通配符）
     * @return 匹配到的键列表
     */
    std::vector<std::string> keys(const std::string& pattern = "*");
    
    /**
     * @brief 清空全部数据
     */
    void clear();
    
    /**
     * @brief 获取当前存储项数量
     * @return 存储中的条目数
     */
    size_t size();
    
    /**
     * @brief 清理所有已过期的键
     */
    void cleanupExpired();
    
    /**
     * @brief 清理存储引擎资源（用于优雅关闭）
     */
    void cleanup();

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    StorageEngine() = default;
    
    mutable std::mutex mutex_;                                                ///< 线程安全互斥锁
    std::unordered_map<std::string, std::shared_ptr<StorageItem>> data_;      ///< 内部键值存储表
};
