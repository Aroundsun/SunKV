#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

// 前向声明
template<typename K, typename V> class LRUCache;
template<typename K, typename V> class LFUCache;
template<typename K, typename V> class ARCCache;

// 缓存策略类型
enum class CachePolicyType {
    LRU,
    LFU,
    ARC
};

// 缓存策略接口
template<typename K, typename V>
class CachePolicy {
public:
    virtual ~CachePolicy() = default;
    
    // 获取缓存项
    virtual bool get(const K& key, V& value) = 0;
    
    // 设置缓存项
    virtual void put(const K& key, const V& value) = 0;
    
    // 删除缓存项
    virtual bool remove(const K& key) = 0;
    
    // 检查键是否存在
    virtual bool contains(const K& key) = 0;
    
    // 获取缓存大小
    virtual size_t size() = 0;
    
    // 获取缓存容量
    virtual size_t capacity() = 0;
    
    // 检查是否为空
    virtual bool empty() = 0;
    
    // 清空缓存
    virtual void clear() = 0;
    
    // 获取策略类型
    virtual CachePolicyType type() = 0;
    
    // 获取策略名称
    virtual std::string name() = 0;
    
    // 获取统计信息
    struct Stats {
        size_t size;
        size_t capacity;
        double utilization;
        std::string extra_info;  // 策略特定的额外信息
    };
    
    virtual Stats get_stats() = 0;
    
    // 获取所有键
    virtual std::vector<K> get_keys() = 0;
};

// LRU 策略包装器
template<typename K, typename V>
class LRUPolicy : public CachePolicy<K, V> {
public:
    explicit LRUPolicy(size_t capacity) : cache_(capacity) {}
    
    bool get(const K& key, V& value) override {
        return cache_.get(key, value);
    }
    
    void put(const K& key, const V& value) override {
        cache_.put(key, value);
    }
    
    bool remove(const K& key) override {
        return cache_.remove(key);
    }
    
    bool contains(const K& key) override {
        return cache_.contains(key);
    }
    
    size_t size() override {
        return cache_.size();
    }
    
    size_t capacity() override {
        return cache_.capacity();
    }
    
    bool empty() override {
        return cache_.empty();
    }
    
    void clear() override {
        cache_.clear();
    }
    
    CachePolicyType type() override {
        return CachePolicyType::LRU;
    }
    
    std::string name() override {
        return "LRU";
    }
    
    Stats get_stats() override {
        auto stats = cache_.get_stats();
        return Stats{
            stats.size,
            stats.capacity,
            stats.utilization,
            ""
        };
    }
    
    std::vector<K> get_keys() override {
        return cache_.get_keys();
    }

private:
    LRUCache<K, V> cache_;
};

// LFU 策略包装器
template<typename K, typename V>
class LFUPolicy : public CachePolicy<K, V> {
public:
    explicit LFUPolicy(size_t capacity) : cache_(capacity) {}
    
    bool get(const K& key, V& value) override {
        return cache_.get(key, value);
    }
    
    void put(const K& key, const V& value) override {
        cache_.put(key, value);
    }
    
    bool remove(const K& key) override {
        return cache_.remove(key);
    }
    
    bool contains(const K& key) override {
        return cache_.contains(key);
    }
    
    size_t size() override {
        return cache_.size();
    }
    
    size_t capacity() override {
        return cache_.capacity();
    }
    
    bool empty() override {
        return cache_.empty();
    }
    
    void clear() override {
        cache_.clear();
    }
    
    CachePolicyType type() override {
        return CachePolicyType::LFU;
    }
    
    std::string name() override {
        return "LFU";
    }
    
    Stats get_stats() override {
        auto stats = cache_.get_stats();
        return Stats{
            stats.size,
            stats.capacity,
            stats.utilization,
            "min_freq=" + std::to_string(stats.min_frequency) + 
            ", max_freq=" + std::to_string(stats.max_frequency)
        };
    }
    
    std::vector<K> get_keys() override {
        return cache_.get_keys();
    }

private:
    LFUCache<K, V> cache_;
};

// ARC 策略包装器
template<typename K, typename V>
class ARCPolicy : public CachePolicy<K, V> {
public:
    explicit ARCPolicy(size_t capacity) : cache_(capacity) {}
    
    bool get(const K& key, V& value) override {
        return cache_.get(key, value);
    }
    
    void put(const K& key, const V& value) override {
        cache_.put(key, value);
    }
    
    bool remove(const K& key) override {
        return cache_.remove(key);
    }
    
    bool contains(const K& key) override {
        return cache_.contains(key);
    }
    
    size_t size() override {
        return cache_.size();
    }
    
    size_t capacity() override {
        return cache_.capacity();
    }
    
    bool empty() override {
        return cache_.empty();
    }
    
    void clear() override {
        cache_.clear();
    }
    
    CachePolicyType type() override {
        return CachePolicyType::ARC;
    }
    
    std::string name() override {
        return "ARC";
    }
    
    Stats get_stats() override {
        auto stats = cache_.get_stats();
        return Stats{
            stats.size,
            stats.capacity,
            stats.utilization,
            "T1=" + std::to_string(stats.t1_size) + 
            ", T2=" + std::to_string(stats.t2_size) + 
            ", B1=" + std::to_string(stats.b1_size) + 
            ", B2=" + std::to_string(stats.b2_size) + 
            ", p=" + std::to_string(stats.p_ratio)
        };
    }
    
    std::vector<K> get_keys() override {
        return cache_.get_keys();
    }

private:
    ARCCache<K, V> cache_;
};

// 缓存策略工厂
template<typename K, typename V>
class CachePolicyFactory {
public:
    static std::unique_ptr<CachePolicy<K, V>> create(CachePolicyType type, size_t capacity) {
        switch (type) {
            case CachePolicyType::LRU:
                return std::make_unique<LRUPolicy<K, V>>(capacity);
            case CachePolicyType::LFU:
                return std::make_unique<LFUPolicy<K, V>>(capacity);
            case CachePolicyType::ARC:
                return std::make_unique<ARCPolicy<K, V>>(capacity);
            default:
                throw std::invalid_argument("Unknown cache policy type");
        }
    }
    
    static std::unique_ptr<CachePolicy<K, V>> create(const std::string& name, size_t capacity) {
        if (name == "LRU" || name == "lru") {
            return create(CachePolicyType::LRU, capacity);
        } else if (name == "LFU" || name == "lfu") {
            return create(CachePolicyType::LFU, capacity);
        } else if (name == "ARC" || name == "arc") {
            return create(CachePolicyType::ARC, capacity);
        } else {
            throw std::invalid_argument("Unknown cache policy name: " + name);
        }
    }
    
    static std::vector<std::string> available_policies() {
        return {"LRU", "LFU", "ARC"};
    }
};
