/**
 * @file CachedStorageEngine.h
 * @brief SunKV 带缓存的存储引擎系统
 * 
 * 本文件包含带缓存功能的存储引擎实现，提供：
 * - LRU (Least Recently Used) 缓存存储引擎
 * - LFU (Least Frequently Used) 缓存存储引擎
 * - ARC (Adaptive Replacement Cache) 缓存存储引擎
 * - 统一的缓存存储引擎接口
 * - 缓存统计和性能监控
 * - 内存使用统计
 */

#pragma once

#include "StorageEngine.h"
#include "LRUCache.h"
#include "LFUCache.h"
#include "ARCCache.h"
#include <memory>
#include <mutex>
#include <chrono>

/**
 * @enum CachePolicyType
 * @brief 缓存策略类型枚举
 * 
 * 定义了支持的缓存策略类型
 */
enum class CachePolicyType {
    LRU,    ///< 最近最少使用
    LFU,    ///< 最少使用频率
    ARC     ///< 自适应替换缓存
};

/**
 * @class CachedStorageEngineBase
 * @brief 带缓存的存储引擎基类
 * 
 * 定义了所有带缓存存储引擎必须实现的通用接口
 */
template<typename K, typename V>
class CachedStorageEngineBase {
public:
    virtual ~CachedStorageEngineBase() = default;
    
    /**
     * @brief 获取值
     * @param key 键
     * @return 值
     */
    virtual V get(const K& key) = 0;
    
    /**
     * @brief 设置键值对
     * @param key 键
     * @param value 值
     * @param ttl_ms 生存时间（毫秒），-1 表示永不过期
     */
    virtual void set(const K& key, const V& value, int64_t ttl_ms = -1) = 0;
    
    /**
     * @brief 删除键
     * @param key 要删除的键
     * @return 是否成功删除
     */
    virtual bool del(const K& key) = 0;
    
    /**
     * @brief 检查键是否存在
     * @param key 要检查的键
     * @return 是否存在
     */
    virtual bool exists(const K& key) = 0;
    
    /**
     * @brief 获取存储大小
     * @return 存储项数量
     */
    virtual size_t size() const = 0;
    
    /**
     * @brief 获取缓存大小
     * @return 缓存项数量
     */
    virtual size_t cache_size() const = 0;
    
    /**
     * @brief 获取缓存容量
     * @return 缓存最大容量
     */
    virtual size_t cache_capacity() const = 0;
    
    /**
     * @brief 清空所有数据
     */
    virtual void clear() = 0;
    
    /**
     * @brief 清空缓存
     */
    virtual void clear_cache() = 0;
    
    /**
     * @brief 清理过期项
     */
    virtual void cleanup_expired() = 0;
    
    /**
     * @brief 缓存统计信息结构
     */
    struct CacheStats {
        size_t cache_size;           ///< 缓存大小
        size_t cache_capacity;       ///< 缓存容量
        double cache_utilization;    ///< 缓存利用率
        size_t cache_hits;            ///< 缓存命中次数
        size_t cache_misses;          ///< 缓存未命中次数
        size_t total_requests;       ///< 总请求次数
        double hit_rate;              ///< 命中率
        std::string policy_name;      ///< 策略名称
        std::string policy_extra_info; ///< 策略特定额外信息
    };
    
    /**
     * @brief 获取缓存统计信息
     * @return 缓存统计信息
     */
    virtual CacheStats get_cache_stats() const = 0;
    
    /**
     * @brief 重置缓存统计信息
     */
    virtual void reset_cache_stats() = 0;
    
    /**
     * @brief 切换缓存策略
     * @param new_type 新的缓存策略类型
     */
    virtual void switch_cache_policy(CachePolicyType new_type) = 0;
    
    /**
     * @brief 内存统计信息结构
     */
    struct MemoryStats {
        size_t storage_memory;  ///< 存储内存使用量
        size_t cache_memory;     ///< 缓存内存使用量
        size_t total_memory;     ///< 总内存使用量
    };
    
    /**
     * @brief 获取内存统计信息
     * @return 内存统计信息
     */
    virtual MemoryStats get_memory_stats() const = 0;
};

/**
 * @class CachedStorageEngineLRU
 * @brief LRU 缓存存储引擎特化
 * 
 * 实现基于 LRU 策略的缓存存储引擎
 */
template<typename K, typename V>
class CachedStorageEngineLRU : public CachedStorageEngineBase<K, V> {
public:
    /**
     * @brief 构造函数
     * @param cache_capacity 缓存容量
     */
    explicit CachedStorageEngineLRU(size_t cache_capacity) : cache_(cache_capacity) {}
    
    V get(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++total_requests_;
        
        V value;
        if (cache_.get(key, value)) {
            ++cache_hits_;
            return value;
        }
        
        ++cache_misses_;
        value = storage_.get(key);
        if (!value.empty()) {
            cache_.put(key, value);
        }
        
        return value;
    }
    
    void set(const K& key, const V& value, int64_t ttl_ms = -1) override {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.set(key, value, ttl_ms);
        if (ttl_ms <= 0) {
            cache_.put(key, value);
        }
    }
    
    bool del(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.remove(key);
        return storage_.del(key);
    }
    
    bool exists(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.contains(key)) {
            return true;
        }
        return storage_.exists(key);
    }
    
    size_t size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.size();
    }
    
    size_t cache_size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }
    
    size_t cache_capacity() const override {
        return cache_.capacity();
    }
    
    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        storage_.clear();
    }
    
    void clear_cache() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    void cleanup_expired() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto all_keys = storage_.keys();
        auto cache_keys = cache_.get_keys();
        for (const auto& cache_key : cache_keys) {
            if (std::find(all_keys.begin(), all_keys.end(), cache_key) == all_keys.end()) {
                cache_.remove(cache_key);
            }
        }
        storage_.cleanupExpired();
    }
    
    typename CachedStorageEngineBase<K, V>::CacheStats get_cache_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto stats = cache_.get_stats();
        double hit_rate = total_requests_ > 0 ? 
            static_cast<double>(cache_hits_) / total_requests_ : 0.0;
        
        return typename CachedStorageEngineBase<K, V>::CacheStats{
            stats.size,
            stats.capacity,
            stats.utilization,
            cache_hits_,
            cache_misses_,
            total_requests_,
            hit_rate,
            "LRU",
            ""
        };
    }
    
    void reset_cache_stats() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_hits_ = 0;
        cache_misses_ = 0;
        total_requests_ = 0;
    }
    
    void switch_cache_policy(CachePolicyType new_type) override {
        // 获取当前数据
        auto current_keys = cache_.get_keys();
        std::vector<std::pair<K, V>> current_data;
        
        for (const auto& key : current_keys) {
            V value;
            if (cache_.get(key, value)) {
                current_data.emplace_back(key, value);
            }
        }
        
        // 这里简化实现，实际应该创建新的引擎实例
        // 暂时不支持动态切换
    }
    
    typename CachedStorageEngineBase<K, V>::MemoryStats get_memory_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t storage_memory = storage_.size() * (sizeof(K) + sizeof(V) + 64);
        size_t cache_memory = cache_.size() * (sizeof(K) + sizeof(V) + 32);
        
        return typename CachedStorageEngineBase<K, V>::MemoryStats{
            storage_memory,
            cache_memory,
            storage_memory + cache_memory
        };
    }

private:
    mutable std::mutex mutex_;                                    ///< 互斥锁，保证线程安全
    StorageEngine& storage_ = StorageEngine::getInstance();        ///< 底层存储引擎实例
    LRUCache<K, V> cache_;                                          ///< LRU 缓存实例
    
    mutable size_t cache_hits_ = 0;                                ///< 缓存命中次数
    mutable size_t cache_misses_ = 0;                              ///< 缓存未命中次数
    mutable size_t total_requests_ = 0;                            ///< 总请求次数
};

/**
 * @class CachedStorageEngineLFU
 * @brief LFU 缓存存储引擎特化
 * 
 * 实现基于 LFU 策略的缓存存储引擎
 */
template<typename K, typename V>
class CachedStorageEngineLFU : public CachedStorageEngineBase<K, V> {
public:
    /**
     * @brief 构造函数
     * @param cache_capacity 缓存容量
     */
    explicit CachedStorageEngineLFU(size_t cache_capacity) : cache_(cache_capacity) {}
    
    V get(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++total_requests_;
        
        V value;
        if (cache_.get(key, value)) {
            ++cache_hits_;
            return value;
        }
        
        ++cache_misses_;
        value = storage_.get(key);
        if (!value.empty()) {
            cache_.put(key, value);
        }
        
        return value;
    }
    
    void set(const K& key, const V& value, int64_t ttl_ms = -1) override {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.set(key, value, ttl_ms);
        if (ttl_ms <= 0) {
            cache_.put(key, value);
        }
    }
    
    bool del(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.remove(key);
        return storage_.del(key);
    }
    
    bool exists(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.contains(key)) {
            return true;
        }
        return storage_.exists(key);
    }
    
    size_t size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.size();
    }
    
    size_t cache_size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }
    
    size_t cache_capacity() const override {
        return cache_.capacity();
    }
    
    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        storage_.clear();
    }
    
    void clear_cache() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    void cleanup_expired() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto all_keys = storage_.keys();
        auto cache_keys = cache_.get_keys();
        for (const auto& cache_key : cache_keys) {
            if (std::find(all_keys.begin(), all_keys.end(), cache_key) == all_keys.end()) {
                cache_.remove(cache_key);
            }
        }
        storage_.cleanupExpired();
    }
    
    typename CachedStorageEngineBase<K, V>::CacheStats get_cache_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto stats = cache_.get_stats();
        double hit_rate = total_requests_ > 0 ? 
            static_cast<double>(cache_hits_) / total_requests_ : 0.0;
        
        return typename CachedStorageEngineBase<K, V>::CacheStats{
            stats.size,
            stats.capacity,
            stats.utilization,
            cache_hits_,
            cache_misses_,
            total_requests_,
            hit_rate,
            "LFU",
            "min_freq=" + std::to_string(stats.min_frequency) + 
            ", max_freq=" + std::to_string(stats.max_frequency)
        };
    }
    
    void reset_cache_stats() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_hits_ = 0;
        cache_misses_ = 0;
        total_requests_ = 0;
    }
    
    void switch_cache_policy(CachePolicyType new_type) override {
        // 暂时不支持动态切换
    }
    
    typename CachedStorageEngineBase<K, V>::MemoryStats get_memory_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t storage_memory = storage_.size() * (sizeof(K) + sizeof(V) + 64);
        size_t cache_memory = cache_.size() * (sizeof(K) + sizeof(V) + 32);
        
        return typename CachedStorageEngineBase<K, V>::MemoryStats{
            storage_memory,
            cache_memory,
            storage_memory + cache_memory
        };
    }

private:
    mutable std::mutex mutex_;                                    ///< 互斥锁，保证线程安全
    StorageEngine& storage_ = StorageEngine::getInstance();        ///< 底层存储引擎实例
    LFUCache<K, V> cache_;                                          ///< LFU 缓存实例
    
    mutable size_t cache_hits_ = 0;                                ///< 缓存命中次数
    mutable size_t cache_misses_ = 0;                              ///< 缓存未命中次数
    mutable size_t total_requests_ = 0;                            ///< 总请求次数
};

/**
 * @class CachedStorageEngineARC
 * @brief ARC 缓存存储引擎特化
 * 
 * 实现基于 ARC 策略的缓存存储引擎
 */
template<typename K, typename V>
class CachedStorageEngineARC : public CachedStorageEngineBase<K, V> {
public:
    /**
     * @brief 构造函数
     * @param cache_capacity 缓存容量
     */
    explicit CachedStorageEngineARC(size_t cache_capacity) : cache_(cache_capacity) {}
    
    V get(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++total_requests_;
        
        V value;
        if (cache_.get(key, value)) {
            ++cache_hits_;
            return value;
        }
        
        ++cache_misses_;
        value = storage_.get(key);
        if (!value.empty()) {
            cache_.put(key, value);
        }
        
        return value;
    }
    
    void set(const K& key, const V& value, int64_t ttl_ms = -1) override {
        std::lock_guard<std::mutex> lock(mutex_);
        storage_.set(key, value, ttl_ms);
        if (ttl_ms <= 0) {
            cache_.put(key, value);
        }
    }
    
    bool del(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.remove(key);
        return storage_.del(key);
    }
    
    bool exists(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cache_.contains(key)) {
            return true;
        }
        return storage_.exists(key);
    }
    
    size_t size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.size();
    }
    
    size_t cache_size() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }
    
    size_t cache_capacity() const override {
        return cache_.capacity();
    }
    
    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        storage_.clear();
    }
    
    void clear_cache() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
    }
    
    void cleanup_expired() override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto all_keys = storage_.keys();
        auto cache_keys = cache_.get_keys();
        for (const auto& cache_key : cache_keys) {
            if (std::find(all_keys.begin(), all_keys.end(), cache_key) == all_keys.end()) {
                cache_.remove(cache_key);
            }
        }
        storage_.cleanupExpired();
    }
    
    typename CachedStorageEngineBase<K, V>::CacheStats get_cache_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto stats = cache_.get_stats();
        double hit_rate = total_requests_ > 0 ? 
            static_cast<double>(cache_hits_) / total_requests_ : 0.0;
        
        return typename CachedStorageEngineBase<K, V>::CacheStats{
            stats.size,
            stats.capacity,
            stats.utilization,
            cache_hits_,
            cache_misses_,
            total_requests_,
            hit_rate,
            "ARC",
            "T1=" + std::to_string(stats.t1_size) + 
            ", T2=" + std::to_string(stats.t2_size) + 
            ", B1=" + std::to_string(stats.b1_size) + 
            ", B2=" + std::to_string(stats.b2_size)
        };
    }
    
    void reset_cache_stats() override {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_hits_ = 0;
        cache_misses_ = 0;
        total_requests_ = 0;
    }
    
    void switch_cache_policy(CachePolicyType new_type) override {
        // 暂时不支持动态切换
    }
    
    typename CachedStorageEngineBase<K, V>::MemoryStats get_memory_stats() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t storage_memory = storage_.size() * (sizeof(K) + sizeof(V) + 64);
        size_t cache_memory = cache_.size() * (sizeof(K) + sizeof(V) + 32);
        
        return typename CachedStorageEngineBase<K, V>::MemoryStats{
            storage_memory,
            cache_memory,
            storage_memory + cache_memory
        };
    }

private:
    mutable std::mutex mutex_;                                    ///< 互斥锁，保证线程安全
    StorageEngine& storage_ = StorageEngine::getInstance();        ///< 底层存储引擎实例
    ARCCache<K, V> cache_;                                          ///< ARC 缓存实例
    
    mutable size_t cache_hits_ = 0;                                ///< 缓存命中次数
    mutable size_t cache_misses_ = 0;                              ///< 缓存未命中次数
    mutable size_t total_requests_ = 0;                            ///< 总请求次数
};

/**
 * @class CachedStorageEngine
 * @brief 缓存存储引擎工厂
 * 
 * 提供创建不同缓存策略存储引擎实例的工厂方法
 */
template<typename K, typename V>
class CachedStorageEngine {
public:
    /**
     * @brief 创建缓存存储引擎实例
     * @param cache_capacity 缓存容量
     * @param policy_type 缓存策略类型（默认为 LRU）
     * @return 缓存存储引擎实例
     */
    static std::unique_ptr<CachedStorageEngineBase<K, V>> create(size_t cache_capacity, 
                                                               CachePolicyType policy_type = CachePolicyType::LRU) {
        switch (policy_type) {
            case CachePolicyType::LRU:
                return std::make_unique<CachedStorageEngineLRU<K, V>>(cache_capacity);
            case CachePolicyType::LFU:
                return std::make_unique<CachedStorageEngineLFU<K, V>>(cache_capacity);
            case CachePolicyType::ARC:
                return std::make_unique<CachedStorageEngineARC<K, V>>(cache_capacity);
        }
        return nullptr;
    }
};
