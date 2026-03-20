#pragma once

#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ARC 缓存节点
template<typename K, typename V>
struct ARCNode {
    K key;
    V value;
    
    ARCNode(const K& k, const V& v) : key(k), value(v) {}
};

// ARC 缓存实现
template<typename K, typename V>
class ARCCache {
public:
    explicit ARCCache(size_t capacity) : capacity_(capacity), p_(0) {
        if (capacity == 0) {
            throw std::invalid_argument("ARCCache capacity must be > 0");
        }
    }
    
    ~ARCCache() = default;
    
    // 获取缓存项
    bool get(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查 T1 (最近只访问一次)
        auto t1_it = t1_map_.find(key);
        if (t1_it != t1_map_.end()) {
            value = (*t1_it->second)->value;
            // 从 T1 移到 T2
            move_t1_to_t2(t1_it->second);
            return true;
        }
        
        // 检查 T2 (最近多次访问)
        auto t2_it = t2_map_.find(key);
        if (t2_it != t2_map_.end()) {
            value = (*t2_it->second)->value;
            // 移到 T2 前部（表示最近访问）
            t2_.splice(t2_.begin(), t2_, t2_it->second);
            return true;
        }
        
        // 检查 B1 (历史只访问一次)
        auto b1_it = b1_map_.find(key);
        if (b1_it != b1_map_.end()) {
            // 命中 B1，增加 p
            adapt_on_b1_hit();
            return false;
        }
        
        // 检查 B2 (历史多次访问)
        auto b2_it = b2_map_.find(key);
        if (b2_it != b2_map_.end()) {
            // 命中 B2，减少 p
            adapt_on_b2_hit();
            return false;
        }
        
        return false;  // 未找到
    }
    
    // 设置缓存项
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (capacity_ == 0) return;
        
        // 检查是否已存在
        if (replace_existing(key, value)) {
            return;
        }
        
        // 新项，先放入 T1
        auto node = std::make_shared<ARCNode<K, V>>(key, value);
        t1_.push_front(node);
        t1_map_[key] = t1_.begin();
        
        // 检查是否需要替换
        replace();
    }
    
    // 删除缓存项
    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 在所有四个列表中查找并删除
        return remove_from_list(key, t1_, t1_map_) ||
               remove_from_list(key, t2_, t2_map_) ||
               remove_from_list(key, b1_, b1_map_) ||
               remove_from_list(key, b2_, b2_map_);
    }
    
    // 检查键是否存在
    bool contains(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return t1_map_.find(key) != t1_map_.end() ||
               t2_map_.find(key) != t2_map_.end();
    }
    
    // 获取缓存大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return t1_map_.size() + t2_map_.size();
    }
    
    // 获取缓存容量
    size_t capacity() const {
        return capacity_;
    }
    
    // 检查是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return t1_map_.empty() && t2_map_.empty();
    }
    
    // 清空缓存
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        t1_.clear();
        t2_.clear();
        b1_.clear();
        b2_.clear();
        t1_map_.clear();
        t2_map_.clear();
        b1_map_.clear();
        b2_map_.clear();
        p_ = 0;
    }
    
    // 获取缓存统计信息
    struct Stats {
        size_t size;
        size_t capacity;
        double utilization;
        size_t t1_size;
        size_t t2_size;
        size_t b1_size;
        size_t b2_size;
        double p_ratio;
    };
    
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats stats;
        stats.size = t1_map_.size() + t2_map_.size();
        stats.capacity = capacity_;
        stats.utilization = capacity_ > 0 ? static_cast<double>(stats.size) / capacity_ : 0.0;
        stats.t1_size = t1_map_.size();
        stats.t2_size = t2_map_.size();
        stats.b1_size = b1_map_.size();
        stats.b2_size = b2_map_.size();
        stats.p_ratio = capacity_ > 0 ? static_cast<double>(p_) / capacity_ : 0.0;
        return stats;
    }
    
    // 获取所有键
    std::vector<K> get_keys() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<K> keys;
        keys.reserve(t1_map_.size() + t2_map_.size());
        
        // T1 中的键
        for (const auto& node : t1_) {
            keys.push_back(node->key);
        }
        
        // T2 中的键
        for (const auto& node : t2_) {
            keys.push_back(node->key);
        }
        
        return keys;
    }

private:
    // 替换已存在的项
    bool replace_existing(const K& key, const V& value) {
        auto t1_it = t1_map_.find(key);
        if (t1_it != t1_map_.end()) {
            (*t1_it->second)->value = value;
            t1_.splice(t1_.begin(), t1_, t1_it->second);
            return true;
        }
        
        auto t2_it = t2_map_.find(key);
        if (t2_it != t2_map_.end()) {
            (*t2_it->second)->value = value;
            t2_.splice(t2_.begin(), t2_, t2_it->second);
            return true;
        }
        
        return false;
    }
    
    // 从 T1 移到 T2
    void move_t1_to_t2(typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator node_it) {
        auto node = *node_it;
        K key = node->key;
        
        // 从 T1 移除
        t1_.erase(node_it);
        t1_map_.erase(key);
        
        // 添加到 T2 前部
        t2_.push_front(node);
        t2_map_[key] = t2_.begin();
    }
    
    // 替换策略
    void replace() {
        size_t total_size = t1_map_.size() + t2_map_.size();
        
        if (total_size <= capacity_) {
            return;  // 不需要替换
        }
        
        if (total_size >= 2 * capacity_) {
            // 需要从 B1 或 B2 中删除
            if (b1_map_.size() >= b2_map_.size()) {
                delete_from_lru(b1_, b1_map_);
            } else {
                delete_from_lru(b2_, b2_map_);
            }
        }
        
        // 从 T1 或 T2 中删除
        if (t1_map_.size() > 0 && (t1_map_.size() > p_ || t2_map_.empty())) {
            delete_from_lru(t1_, t1_map_);
        } else if (t2_map_.size() > 0) {
            delete_from_lru(t2_, t2_map_);
        }
    }
    
    // 从列表尾部删除
    void delete_from_lru(std::list<std::shared_ptr<ARCNode<K, V>>>& list,
                        std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator>& map) {
        if (list.empty()) return;
        
        auto node = list.back();
        map.erase(node->key);
        list.pop_back();
    }
    
    // 从指定列表删除
    bool remove_from_list(const K& key,
                         std::list<std::shared_ptr<ARCNode<K, V>>>& list,
                         std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator>& map) {
        auto it = map.find(key);
        if (it != map.end()) {
            list.erase(it->second);
            map.erase(it);
            return true;
        }
        return false;
    }
    
    // B1 命中时的适应
    void adapt_on_b1_hit() {
        size_t delta = 1;
        if (b1_map_.size() > b2_map_.size()) {
            delta = b1_map_.size() - b2_map_.size();
        }
        
        p_ = std::min(p_ + delta, capacity_);
        
        // 只有在真正需要时才移动数据
        if (t1_map_.size() + t2_map_.size() > capacity_) {
            if (t2_map_.size() > 0) {
                auto node = t2_.back();
                t2_.pop_back();
                t2_map_.erase(node->key);
                
                b2_.push_front(node);
                b2_map_[node->key] = b2_.begin();
            }
        }
    }
    
    // B2 命中时的适应
    void adapt_on_b2_hit() {
        size_t delta = 1;
        if (b2_map_.size() > b1_map_.size()) {
            delta = b2_map_.size() - b1_map_.size();
        }
        
        p_ = std::max(p_ - delta, static_cast<size_t>(0));
        
        // 只有在真正需要时才移动数据
        if (t1_map_.size() + t2_map_.size() > capacity_) {
            if (t1_map_.size() > 0) {
                auto node = t1_.back();
                t1_.pop_back();
                t1_map_.erase(node->key);
                
                b1_.push_front(node);
                b1_map_[node->key] = b1_.begin();
            }
        }
    }
    
    mutable std::mutex mutex_;
    size_t capacity_;
    size_t p_;  // 自适应参数
    
    // T1: 最近只访问一次
    std::list<std::shared_ptr<ARCNode<K, V>>> t1_;
    std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator> t1_map_;
    
    // T2: 最近多次访问
    std::list<std::shared_ptr<ARCNode<K, V>>> t2_;
    std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator> t2_map_;
    
    // B1: 历史只访问一次
    std::list<std::shared_ptr<ARCNode<K, V>>> b1_;
    std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator> b1_map_;
    
    // B2: 历史多次访问
    std::list<std::shared_ptr<ARCNode<K, V>>> b2_;
    std::unordered_map<K, typename std::list<std::shared_ptr<ARCNode<K, V>>>::iterator> b2_map_;
};
