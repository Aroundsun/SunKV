#pragma once

#include "ShardedKVStore.h"
#include "CachePolicy.h"
#include <memory>
#include <mutex>
#include <vector>

// 带缓存的分片存储引擎
template<typename K, typename V>
class CachedShardedKVStore {
public:
    explicit CachedShardedKVStore(size_t shard_count = 16, 
                                  size_t cache_capacity_per_shard = 1000,
                                  CachePolicyType policy_type = CachePolicyType::LRU)
        : shard_count_(shard_count) {
        
        // 初始化分片存储
        sharded_store_ = std::make_unique<ShardedKVStore>(shard_count);
        
        // 为每个分片创建缓存
        caches_.reserve(shard_count);
        for (size_t i = 0; i < shard_count; ++i) {
            caches_.push_back(CachePolicyFactory<K, V>::create(policy_type, cache_capacity_per_shard));
        }
        
        // 初始化统计
        cache_stats_.resize(shard_count);
        for (auto& stats : cache_stats_) {
            stats = {0, 0, 0};
        }
    }
    
    ~CachedShardedKVStore() = default;
    
    // 获取值
    V get(const K& key) {
        size_t shard_idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
        
        auto& cache = caches_[shard_idx];
        auto& stats = cache_stats_[shard_idx];
        
        ++stats.total_requests;
        
        V value;
        if (cache->get(key, value)) {
            ++stats.cache_hits;
            return value;
        }
        
        ++stats.cache_misses;
        // 从分片存储获取
        value = sharded_store_->get(key);
        if (!value.empty()) {
            cache->put(key, value);
        }
        
        return value;
    }
    
    // 设置值
    void set(const K& key, const V& value, int64_t ttl_ms = -1) {
        size_t shard_idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
        
        // 更新分片存储
        sharded_store_->set(key, value, ttl_ms);
        
        // 更新对应分片的缓存
        if (ttl_ms <= 0) {  // 只有非过期键才缓存
            caches_[shard_idx]->put(key, value);
        }
    }
    
    // 删除键
    bool del(const K& key) {
        size_t shard_idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
        
        // 从缓存删除
        caches_[shard_idx]->remove(key);
        
        // 从分片存储删除
        return sharded_store_->del(key);
    }
    
    // 检查键是否存在
    bool exists(const K& key) {
        size_t shard_idx = get_shard_index(key);
        std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
        
        if (caches_[shard_idx]->contains(key)) {
            return true;
        }
        
        return sharded_store_->exists(key);
    }
    
    // 批量获取
    std::vector<V> mget(const std::vector<K>& keys) {
        // 按分片分组
        std::unordered_map<size_t, std::vector<std::pair<size_t, K>>> shard_groups;
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t shard_idx = get_shard_index(keys[i]);
            shard_groups[shard_idx].emplace_back(i, keys[i]);
        }
        
        std::vector<V> results(keys.size());
        
        // 按分片顺序处理
        std::vector<size_t> sorted_shards;
        for (const auto& group : shard_groups) {
            sorted_shards.push_back(group.first);
        }
        std::sort(sorted_shards.begin(), sorted_shards.end());
        
        for (size_t shard_idx : sorted_shards) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
            
            const auto& group = shard_groups[shard_idx];
            auto& cache = caches_[shard_idx];
            auto& stats = cache_stats_[shard_idx];
            
            std::vector<K> missed_keys;
            std::vector<size_t> missed_indices;
            
            // 从缓存获取
            for (const auto& pair : group) {
                size_t original_idx = pair.first;
                const K& key = pair.second;
                
                ++stats.total_requests;
                
                V value;
                if (cache->get(key, value)) {
                    ++stats.cache_hits;
                    results[original_idx] = value;
                } else {
                    ++stats.cache_misses;
                    missed_keys.push_back(key);
                    missed_indices.push_back(original_idx);
                }
            }
            
            // 从存储获取未命中的键
            if (!missed_keys.empty()) {
                auto storage_results = sharded_store_->mget(missed_keys);
                for (size_t i = 0; i < storage_results.size(); ++i) {
                    size_t original_idx = missed_indices[i];
                    results[original_idx] = storage_results[i];
                    
                    // 缓存结果
                    if (!storage_results[i].empty()) {
                        cache->put(missed_keys[i], storage_results[i]);
                    }
                }
            }
        }
        
        return results;
    }
    
    // 批量设置
    void mset(const std::vector<std::pair<K, V>>& key_values) {
        // 按分片分组
        std::unordered_map<size_t, std::vector<std::pair<K, V>>> shard_groups;
        for (const auto& kv : key_values) {
            size_t shard_idx = get_shard_index(kv.first);
            shard_groups[shard_idx].push_back(kv);
        }
        
        // 按分片顺序处理
        std::vector<size_t> sorted_shards;
        for (const auto& group : shard_groups) {
            sorted_shards.push_back(group.first);
        }
        std::sort(sorted_shards.begin(), sorted_shards.end());
        
        for (size_t shard_idx : sorted_shards) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
            
            const auto& group = shard_groups[shard_idx];
            
            // 构造键向量
            std::vector<K> keys;
            keys.reserve(group.size());
            for (const auto& kv : group) {
                keys.push_back(kv.first);
            }
            
            // 更新存储
            sharded_store_->mset(group);
            
            // 更新缓存
            auto& cache = caches_[shard_idx];
            for (const auto& kv : group) {
                cache->put(kv.first, kv.second);
            }
        }
    }
    
    // 批量删除
    int mdel(const std::vector<K>& keys) {
        // 按分片分组
        std::unordered_map<size_t, std::vector<K>> shard_groups;
        for (const auto& key : keys) {
            size_t shard_idx = get_shard_index(key);
            shard_groups[shard_idx].push_back(key);
        }
        
        int total_deleted = 0;
        
        // 按分片顺序处理
        std::vector<size_t> sorted_shards;
        for (const auto& group : shard_groups) {
            sorted_shards.push_back(group.first);
        }
        std::sort(sorted_shards.begin(), sorted_shards.end());
        
        for (size_t shard_idx : sorted_shards) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[shard_idx]);
            
            const auto& group = shard_groups[shard_idx];
            auto& cache = caches_[shard_idx];
            
            // 从缓存删除
            for (const auto& key : group) {
                cache->remove(key);
            }
            
            // 从存储删除
            total_deleted += sharded_store_->mdel(group);
        }
        
        return total_deleted;
    }
    
    // 获取存储大小
    size_t size() const {
        return sharded_store_->size();
    }
    
    // 获取分片数量
    size_t shard_count() const {
        return shard_count_;
    }
    
    // 获取分片大小
    std::vector<size_t> shard_sizes() const {
        return sharded_store_->shard_sizes();
    }
    
    // 清空所有数据
    void clear() {
        // 清空所有缓存
        for (size_t i = 0; i < shard_count_; ++i) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[i]);
            caches_[i]->clear();
        }
        
        // 清空存储
        sharded_store_->clear();
    }
    
    // 清理过期键
    void cleanup_expired() {
        // 清理存储中的过期键
        sharded_store_->cleanup_expired();
        
        // 清理缓存中不存在的键
        for (size_t i = 0; i < shard_count_; ++i) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[i]);
            
            auto& cache = caches_[i];
            auto cache_keys = cache->get_keys();
            
            for (const auto& key : cache_keys) {
                if (!sharded_store_->exists(key)) {
                    cache->remove(key);
                }
            }
        }
    }
    
    // 获取缓存统计信息
    struct ShardCacheStats {
        size_t cache_size;
        size_t cache_capacity;
        double cache_utilization;
        size_t cache_hits;
        size_t cache_misses;
        size_t total_requests;
        double hit_rate;
        std::string policy_name;
    };
    
    std::vector<ShardCacheStats> get_cache_stats() const {
        std::vector<ShardCacheStats> all_stats;
        all_stats.reserve(shard_count_);
        
        for (size_t i = 0; i < shard_count_; ++i) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[i]);
            
            auto& cache = caches_[i];
            auto& stats = cache_stats_[i];
            
            auto policy_stats = cache->get_stats();
            double hit_rate = stats.total_requests > 0 ? 
                static_cast<double>(stats.cache_hits) / stats.total_requests : 0.0;
            
            all_stats.push_back(ShardCacheStats{
                policy_stats.size,
                policy_stats.capacity,
                policy_stats.utilization,
                stats.cache_hits,
                stats.cache_misses,
                stats.total_requests,
                hit_rate,
                cache->name()
            });
        }
        
        return all_stats;
    }
    
    // 获取总体缓存统计
    struct OverallCacheStats {
        size_t total_cache_size;
        size_t total_cache_capacity;
        double total_cache_utilization;
        size_t total_cache_hits;
        size_t total_cache_misses;
        size_t total_requests;
        double overall_hit_rate;
        std::vector<ShardCacheStats> shard_stats;
    };
    
    OverallCacheStats get_overall_cache_stats() const {
        auto shard_stats = get_cache_stats();
        
        OverallCacheStats overall;
        overall.total_cache_size = 0;
        overall.total_cache_capacity = 0;
        overall.total_cache_hits = 0;
        overall.total_cache_misses = 0;
        overall.total_requests = 0;
        
        for (const auto& shard : shard_stats) {
            overall.total_cache_size += shard.cache_size;
            overall.total_cache_capacity += shard.cache_capacity;
            overall.total_cache_hits += shard.cache_hits;
            overall.total_cache_misses += shard.cache_misses;
            overall.total_requests += shard.total_requests;
        }
        
        overall.total_cache_utilization = overall.total_cache_capacity > 0 ? 
            static_cast<double>(overall.total_cache_size) / overall.total_cache_capacity : 0.0;
        
        overall.overall_hit_rate = overall.total_requests > 0 ? 
            static_cast<double>(overall.total_cache_hits) / overall.total_requests : 0.0;
        
        overall.shard_stats = std::move(shard_stats);
        
        return overall;
    }
    
    // 重置缓存统计
    void reset_cache_stats() {
        for (size_t i = 0; i < shard_count_; ++i) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[i]);
            cache_stats_[i] = {0, 0, 0};
        }
    }
    
    // 切换缓存策略
    void switch_cache_policy(CachePolicyType new_type) {
        for (size_t i = 0; i < shard_count_; ++i) {
            std::lock_guard<std::mutex> lock(shard_mutexes_[i]);
            
            auto& old_cache = caches_[i];
            
            // 获取当前缓存数据
            auto current_keys = old_cache->get_keys();
            std::vector<std::pair<K, V>> current_data;
            
            for (const auto& key : current_keys) {
                V value;
                if (old_cache->get(key, value)) {
                    current_data.emplace_back(key, value);
                }
            }
            
            // 创建新策略
            caches_[i] = CachePolicyFactory<K, V>::create(new_type, old_cache->capacity());
            
            // 重新加载数据
            for (const auto& kv : current_data) {
                caches_[i]->put(kv.first, kv.second);
            }
        }
    }

private:
    size_t get_shard_index(const K& key) const {
        return sharded_store_->get_shard_index(key);
    }
    
    // 分片统计结构
    struct ShardStats {
        size_t cache_hits;
        size_t cache_misses;
        size_t total_requests;
    };
    
    size_t shard_count_;
    std::unique_ptr<ShardedKVStore> sharded_store_;
    std::vector<std::unique_ptr<CachePolicy<K, V>>> caches_;
    std::vector<std::mutex> shard_mutexes_;
    std::vector<ShardStats> cache_stats_;
};
