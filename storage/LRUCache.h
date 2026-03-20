#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <string>

// LRU 缓存节点
template<typename K, typename V>
struct LRUNode {
    K key;
    V value;
    
    LRUNode(const K& k, const V& v) : key(k), value(v) {}
};

// LRU 缓存实现
template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("LRUCache capacity must be > 0");
        }
    }
    
    ~LRUCache() = default;
    
    // 获取缓存项
    bool get(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;  // 未找到
        }
        
        // 将访问的节点移到链表头部
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        
        value = (*it->second)->value;
        return true;
    }
    
    // 设置缓存项
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // 键已存在，更新值并移到头部
            (*it->second)->value = value;
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        } else {
            // 键不存在，添加新节点
            if (cache_list_.size() >= capacity_) {
                // 容量已满，移除尾部节点
                evict();
            }
            
            cache_list_.push_front(std::make_shared<LRUNode<K, V>>(key, value));
            cache_map_[key] = cache_list_.begin();
        }
    }
    
    // 删除缓存项
    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;  // 未找到
        }
        
        // 从链表中移除
        cache_list_.erase(it->second);
        cache_map_.erase(it);
        return true;
    }
    
    // 检查键是否存在
    bool contains(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_map_.find(key) != cache_map_.end();
    }
    
    // 获取缓存大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_list_.size();
    }
    
    // 获取缓存容量
    size_t capacity() const {
        return capacity_;
    }
    
    // 检查是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_list_.empty();
    }
    
    // 清空缓存
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_list_.clear();
        cache_map_.clear();
    }
    
    // 获取缓存统计信息
    struct Stats {
        size_t size;
        size_t capacity;
        double utilization;
    };
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats;
        stats.size = cache_list_.size();
        stats.capacity = capacity_;
        stats.utilization = capacity_ > 0 ? static_cast<double>(stats.size) / capacity_ : 0.0;
        return stats;
    }
    
    // 获取所有键（按 LRU 顺序）
    std::vector<K> get_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<K> keys;
        keys.reserve(cache_list_.size());
        
        for (const auto& node : cache_list_) {
            keys.push_back(node->key);
        }
        return keys;
    }

private:
    // 淘汰最久未使用的节点
    void evict() {
        if (!cache_list_.empty()) {
            auto last = cache_list_.end();
            --last;
            cache_map_.erase((*last)->key);
            cache_list_.pop_back();
        }
    }
    
    mutable std::mutex mutex_;
    size_t capacity_;
    std::list<std::shared_ptr<LRUNode<K, V>>> cache_list_;
    std::unordered_map<K, typename std::list<std::shared_ptr<LRUNode<K, V>>>::iterator> cache_map_;
};
