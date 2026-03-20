#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <string>

// LFU 缓存节点
template<typename K, typename V>
struct LFUNode {
    K key;
    V value;
    int frequency;
    
    LFUNode(const K& k, const V& v) : key(k), value(v), frequency(1) {}
};

// LFU 缓存实现
template<typename K, typename V>
class LFUCache {
public:
    explicit LFUCache(size_t capacity) : capacity_(capacity), min_frequency_(0) {
        if (capacity == 0) {
            throw std::invalid_argument("LFUCache capacity must be > 0");
        }
    }
    
    ~LFUCache() = default;
    
    // 获取缓存项
    bool get(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;  // 未找到
        }
        
        auto node = it->second;
        value = node->value;
        
        // 增加频率
        increase_frequency(node);
        
        return true;
    }
    
    // 设置缓存项
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (capacity_ == 0) return;
        
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // 键已存在，更新值并增加频率
            auto node = it->second;
            node->value = value;
            increase_frequency(node);
        } else {
            // 键不存在，添加新节点
            if (cache_map_.size() >= capacity_) {
                // 容量已满，淘汰频率最低的节点
                evict();
            }
            
            auto node = std::make_shared<LFUNode<K, V>>(key, value);
            cache_map_[key] = node;
            
            // 添加到频率为 1 的列表
            frequency_lists_[1].push_back(node);
            node_frequency_map_[node] = 1;
            
            min_frequency_ = 1;
        }
    }
    
    // 删除缓存项
    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;  // 未找到
        }
        
        auto node = it->second;
        int frequency = node_frequency_map_[node];
        
        // 从频率列表中移除
        auto& list = frequency_lists_[frequency];
        list.remove(node);
        
        if (list.empty() && frequency == min_frequency_) {
            // 更新最小频率
            update_min_frequency();
        }
        
        // 清理映射
        cache_map_.erase(it);
        node_frequency_map_.erase(node);
        
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
        return cache_map_.size();
    }
    
    // 获取缓存容量
    size_t capacity() const {
        return capacity_;
    }
    
    // 检查是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_map_.empty();
    }
    
    // 清空缓存
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_map_.clear();
        frequency_lists_.clear();
        node_frequency_map_.clear();
        min_frequency_ = 0;
    }
    
    // 获取缓存统计信息
    struct Stats {
        size_t size;
        size_t capacity;
        double utilization;
        int min_frequency;
        int max_frequency;
    };
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats;
        stats.size = cache_map_.size();
        stats.capacity = capacity_;
        stats.utilization = capacity_ > 0 ? static_cast<double>(stats.size) / capacity_ : 0.0;
        stats.min_frequency = min_frequency_;
        // 找到最大频率
        int max_freq = 0;
        for (const auto& pair : frequency_lists_) {
            if (pair.first > max_freq) {
                max_freq = pair.first;
            }
        }
        stats.max_frequency = max_freq;
        return stats;
    }
    
    // 获取所有键（按频率排序）
    std::vector<K> get_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<K> keys;
        keys.reserve(cache_map_.size());
        
        // 按频率从低到高遍历
        for (const auto& pair : frequency_lists_) {
            for (const auto& node : pair.second) {
                keys.push_back(node->key);
            }
        }
        return keys;
    }

private:
    // 增加节点频率
    void increase_frequency(std::shared_ptr<LFUNode<K, V>> node) {
        int old_frequency = node_frequency_map_[node];
        int new_frequency = old_frequency + 1;
        
        // 从旧频率列表中移除
        auto& old_list = frequency_lists_[old_frequency];
        old_list.remove(node);
        
        // 添加到新频率列表
        frequency_lists_[new_frequency].push_back(node);
        node_frequency_map_[node] = new_frequency;
        
        // 更新最小频率
        if (old_list.empty() && old_frequency == min_frequency_) {
            min_frequency_ = new_frequency;
        }
    }
    
    // 淘汰频率最低的节点
    void evict() {
        if (frequency_lists_.empty() || min_frequency_ == 0) {
            return;
        }
        
        auto& list = frequency_lists_[min_frequency_];
        if (list.empty()) {
            update_min_frequency();
            if (frequency_lists_.empty()) return;
            evict();
            return;
        }
        
        auto node = list.front();
        list.pop_front();
        
        cache_map_.erase(node->key);
        node_frequency_map_.erase(node);
        
        if (list.empty()) {
            update_min_frequency();
        }
    }
    
    // 更新最小频率
    void update_min_frequency() {
        while (min_frequency_ > 0 && frequency_lists_[min_frequency_].empty()) {
            ++min_frequency_;
        }
    }
    
    mutable std::mutex mutex_;
    size_t capacity_;
    int min_frequency_;
    
    std::unordered_map<K, std::shared_ptr<LFUNode<K, V>>> cache_map_;
    std::unordered_map<std::shared_ptr<LFUNode<K, V>>, int> node_frequency_map_;
    std::unordered_map<int, std::list<std::shared_ptr<LFUNode<K, V>>>> frequency_lists_;
};
