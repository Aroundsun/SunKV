#pragma once

#include "StorageEngine.h"
#include "LRUCache.h"
#include "LFUCache.h"
#include "ARCCache.h"
#include <memory>
#include <mutex>
#include <chrono>

// 缓存策略类型
enum class CachePolicyType {
    LRU,
    LFU,
    ARC
};

// 带缓存的存储引擎基类
template<typename K, typename V>
class CachedStorageEngineBase {
public:
    virtual ~CachedStorageEngineBase() = default;
    virtual V get(const K& key) = 0;
    virtual void set(const K& key, const V& value, int64_t ttl_ms = -1) = 0;
    virtual bool del(const K& key) = 0;
    virtual bool exists(const K& key) = 0;
    virtual size_t size() const = 0;
    virtual size_t cache_size() const = 0;
    virtual size_t cache_capacity() const = 0;
    virtual void clear() = 0;
    virtual void clear_cache() = 0;
    virtual void cleanup_expired() = 0;
    
    struct CacheStats {
        size_t cache_size;
        size_t cache_capacity;
        double cache_utilization;
        size_t cache_hits;
        size_t cache_misses;
        size_t total_requests;
        double hit_rate;
        std::string policy_name;
        std::string policy_extra_info;
    };
    
    virtual CacheStats get_cache_stats() const = 0;
    virtual void reset_cache_stats() = 0;
    virtual void switch_cache_policy(CachePolicyType new_type) = 0;
    
    struct MemoryStats {
        size_t storage_memory;
        size_t cache_memory;
        size_t total_memory;
    };
    
    virtual MemoryStats get_memory_stats() const = 0;
};

// LRU 缓存存储引擎特化
template<typename K, typename V>
class CachedStorageEngineLRU : public CachedStorageEngineBase<K, V> {
public:
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
    mutable std::mutex mutex_;
    StorageEngine& storage_ = StorageEngine::getInstance();
    LRUCache<K, V> cache_;
    
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    mutable size_t total_requests_ = 0;
};

// LFU 缓存存储引擎特化
template<typename K, typename V>
class CachedStorageEngineLFU : public CachedStorageEngineBase<K, V> {
public:
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
    mutable std::mutex mutex_;
    StorageEngine& storage_ = StorageEngine::getInstance();
    LFUCache<K, V> cache_;
    
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    mutable size_t total_requests_ = 0;
};

// ARC 缓存存储引擎特化
template<typename K, typename V>
class CachedStorageEngineARC : public CachedStorageEngineBase<K, V> {
public:
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
    mutable std::mutex mutex_;
    StorageEngine& storage_ = StorageEngine::getInstance();
    ARCCache<K, V> cache_;
    
    mutable size_t cache_hits_ = 0;
    mutable size_t cache_misses_ = 0;
    mutable size_t total_requests_ = 0;
};

// 缓存存储引擎工厂
template<typename K, typename V>
class CachedStorageEngine {
public:
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
