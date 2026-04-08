/**
 * @file CachePolicy.h
 * @brief SunKV 缓存策略系统
 * 
 * 本文件包含缓存策略的实现，提供：
 * - LRU (Least Recently Used) 缓存策略
 * - LFU (Least Frequently Used) 缓存策略
 * - ARC (Adaptive Replacement Cache) 缓存策略
 * - 统一的缓存策略接口
 * - 缓存策略工厂模式
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

/// 前向声明
template<typename K, typename V> class LRUCache;
template<typename K, typename V> class LFUCache;
template<typename K, typename V> class ARCCache;

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
 * @class CachePolicy
 * @brief 缓存策略接口
 * 
 * 定义了所有缓存策略必须实现的通用接口
 */
template<typename K, typename V>
class CachePolicy {
public:
    virtual ~CachePolicy() = default;
    
    /**
     * @brief 获取缓存项
     * @param key 缓存键
     * @param value 输出参数，存储找到的值
     * @return 是否找到并获取成功
     */
    virtual bool get(const K& key, V& value) = 0;
    
    /**
     * @brief 设置缓存项
     * @param key 缓存键
     * @param value 缓存值
     */
    virtual void put(const K& key, const V& value) = 0;
    
    /**
     * @brief 删除缓存项
     * @param key 要删除的缓存键
     * @return 是否成功删除
     */
    virtual bool remove(const K& key) = 0;
    
    /**
     * @brief 检查键是否存在
     * @param key 要检查的键
     * @return 是否存在
     */
    virtual bool contains(const K& key) = 0;
    
    /**
     * @brief 获取缓存大小
     * @return 当前缓存项数量
     */
    virtual size_t size() = 0;
    
    /**
     * @brief 获取缓存容量
     * @return 缓存最大容量
     */
    virtual size_t capacity() = 0;
    
    /**
     * @brief 检查是否为空
     * @return 是否为空
     */
    virtual bool empty() = 0;
    
    /**
     * @brief 清空缓存
     */
    virtual void clear() = 0;
    
    /**
     * @brief 获取策略类型
     * @return 策略类型
     */
    virtual CachePolicyType type() = 0;
    
    /**
     * @brief 获取策略名称
     * @return 策略名称字符串
     */
    virtual std::string name() = 0;
    
    /**
     * @brief 缓存统计信息结构
     */
    struct Stats {
        size_t size;                 ///< 当前大小
        size_t capacity;             ///< 容量
        double utilization;          ///< 利用率
        std::string extra_info;      ///< 策略特定的额外信息
    };
    
    /**
     * @brief 获取统计信息
     * @return 统计信息结构
     */
    virtual Stats get_stats() = 0;
    
    /**
     * @brief 获取所有键
     * @return 所有缓存键的列表
     */
    virtual std::vector<K> get_keys() = 0;
};

/**
 * @class LRUPolicy
 * @brief LRU (Least Recently Used) 缓存策略
 * 
 * 实现最近最少使用的缓存替换策略
 */
template<typename K, typename V>
class LRUPolicy : public CachePolicy<K, V> {
public:
    /**
     * @brief 构造函数
     * @param capacity 缓存容量
     */
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

/**
 * @class LFUPolicy
 * @brief LFU (Least Frequently Used) 缓存策略
 * 
 * 实现最少使用频率的缓存替换策略
 */
template<typename K, typename V>
class LFUPolicy : public CachePolicy<K, V> {
public:
    /**
     * @brief 构造函数
     * @param capacity 缓存容量
     */
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

/**
 * @class ARCPolicy
 * @brief ARC (Adaptive Replacement Cache) 缓存策略
 * 
 * 实现自适应替换的缓存策略，结合了 LRU 和 LFU 的优点
 */
template<typename K, typename V>
class ARCPolicy : public CachePolicy<K, V> {
public:
    /**
     * @brief 构造函数
     * @param capacity 缓存容量
     */
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

/**
 * @class CachePolicyFactory
 * @brief 缓存策略工厂类
 * 
 * 提供创建不同缓存策略实例的工厂方法
 */
template<typename K, typename V>
class CachePolicyFactory {
public:
    /**
     * @brief 根据策略类型创建缓存策略
     * @param type 策略类型
     * @param capacity 缓存容量
     * @return 缓存策略实例
     */
    static std::unique_ptr<CachePolicy<K, V>> create(CachePolicyType type, size_t capacity) {
        switch (type) {
            case CachePolicyType::LRU:
                return std::make_unique<LRUPolicy<K, V>>(capacity);
            case CachePolicyType::LFU:
                return std::make_unique<LFUPolicy<K, V>>(capacity);
            case CachePolicyType::ARC:
                return std::make_unique<ARCPolicy<K, V>>(capacity);
            default:
                throw std::invalid_argument("未知的缓存策略类型");
        }
    }
    
    /**
     * @brief 根据策略名称创建缓存策略
     * @param name 策略名称
     * @param capacity 缓存容量
     * @return 缓存策略实例
     */
    static std::unique_ptr<CachePolicy<K, V>> create(const std::string& name, size_t capacity) {
        if (name == "LRU" || name == "lru") {
            return create(CachePolicyType::LRU, capacity);
        } else if (name == "LFU" || name == "lfu") {
            return create(CachePolicyType::LFU, capacity);
        } else if (name == "ARC" || name == "arc") {
            return create(CachePolicyType::ARC, capacity);
        } else {
            throw std::invalid_argument("未知的缓存策略名称: " + name);
        }
    }
    
    /**
     * @brief 获取所有可用的策略名称
     * @return 策略名称列表
     */
    static std::vector<std::string> available_policies() {
        return {"LRU", "LFU", "ARC"};
    }
};
