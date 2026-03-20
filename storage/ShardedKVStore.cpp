#include "ShardedKVStore.h"
#include "StorageEngine.h"
#include <algorithm>
#include <unordered_map>

// FNV-1a 哈希算法实现
uint32_t ShardedKVStore::hash_key(const std::string& key) {
    const uint32_t FNV_PRIME = 16777619;
    const uint32_t FNV_OFFSET_BASIS = 2166136261;
    
    uint32_t hash = FNV_OFFSET_BASIS;
    for (char c : key) {
        hash ^= static_cast<uint32_t>(c);
        hash *= FNV_PRIME;
    }
    
    return hash;
}

ShardedKVStore::ShardedKVStore(size_t shard_count) 
    : shard_count_(shard_count) {
    
    // 初始化分片锁
    shard_locks_ = std::make_unique<std::shared_mutex[]>(shard_count_);
    
    // 初始化存储引擎分片
    shards_.reserve(shard_count_);
    for (size_t i = 0; i < shard_count_; ++i) {
        // 每个分片使用相同的单例实例，但通过不同的锁来模拟分片
        shards_.push_back(std::shared_ptr<StorageEngine>(&StorageEngine::getInstance(), [](StorageEngine*){}));
    }
}

size_t ShardedKVStore::get_shard_index(const std::string& key) const {
    if (shard_count_ == 0) return 0;
    
    uint32_t hash = hash_key(key);
    return hash % shard_count_;
}

bool ShardedKVStore::set(const std::string& key, const std::string& value, int64_t ttl_ms) {
    size_t shard_idx = get_shard_index(key);
    
    // 获取分片的写锁
    std::unique_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
    
    return shards_[shard_idx]->set(key, value, ttl_ms);
}

std::string ShardedKVStore::get(const std::string& key) {
    size_t shard_idx = get_shard_index(key);
    
    // 获取分片的读锁
    std::shared_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
    
    return shards_[shard_idx]->get(key);
}

bool ShardedKVStore::del(const std::string& key) {
    size_t shard_idx = get_shard_index(key);
    
    // 获取分片的写锁
    std::unique_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
    
    return shards_[shard_idx]->del(key);
}

bool ShardedKVStore::exists(const std::string& key) {
    size_t shard_idx = get_shard_index(key);
    
    // 获取分片的读锁
    std::shared_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
    
    return shards_[shard_idx]->exists(key);
}

std::vector<std::string> ShardedKVStore::mget(const std::vector<std::string>& keys) {
    std::vector<std::string> results;
    results.reserve(keys.size());
    
    // 按分片分组键
    std::unordered_map<size_t, std::vector<std::pair<size_t, std::string>>> shard_groups;
    for (size_t i = 0; i < keys.size(); ++i) {
        size_t shard_idx = get_shard_index(keys[i]);
        shard_groups[shard_idx].emplace_back(i, keys[i]);
    }
    
    // 按分片顺序处理，避免死锁
    std::vector<size_t> sorted_shards;
    for (const auto& group : shard_groups) {
        sorted_shards.push_back(group.first);
    }
    std::sort(sorted_shards.begin(), sorted_shards.end());
    
    // 初始化结果数组
    results.resize(keys.size());
    
    // 处理每个分片
    for (size_t shard_idx : sorted_shards) {
        const auto& group = shard_groups[shard_idx];
        
        // 获取分片的读锁
        std::shared_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
        
        // 批量获取
        for (const auto& pair : group) {
            results[pair.first] = shards_[shard_idx]->get(pair.second);
        }
    }
    
    return results;
}

bool ShardedKVStore::mset(const std::vector<std::pair<std::string, std::string>>& key_values) {
    // 按分片分组键值对
    std::unordered_map<size_t, std::vector<std::pair<std::string, std::string>>> shard_groups;
    for (const auto& kv : key_values) {
        size_t shard_idx = get_shard_index(kv.first);
        shard_groups[shard_idx].push_back(kv);
    }
    
    // 按分片顺序处理，避免死锁
    std::vector<size_t> sorted_shards;
    for (const auto& group : shard_groups) {
        sorted_shards.push_back(group.first);
    }
    std::sort(sorted_shards.begin(), sorted_shards.end());
    
    bool all_success = true;
    
    // 处理每个分片
    for (size_t shard_idx : sorted_shards) {
        const auto& group = shard_groups[shard_idx];
        
        // 获取分片的写锁
        std::unique_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
        
        // 批量设置
        for (const auto& kv : group) {
            if (!shards_[shard_idx]->set(kv.first, kv.second)) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

int ShardedKVStore::mdel(const std::vector<std::string>& keys) {
    int deleted_count = 0;
    
    // 按分片分组键
    std::unordered_map<size_t, std::vector<std::string>> shard_groups;
    for (const auto& key : keys) {
        size_t shard_idx = get_shard_index(key);
        shard_groups[shard_idx].push_back(key);
    }
    
    // 按分片顺序处理，避免死锁
    std::vector<size_t> sorted_shards;
    for (const auto& group : shard_groups) {
        sorted_shards.push_back(group.first);
    }
    std::sort(sorted_shards.begin(), sorted_shards.end());
    
    // 处理每个分片
    for (size_t shard_idx : sorted_shards) {
        const auto& group = shard_groups[shard_idx];
        
        // 获取分片的写锁
        std::unique_lock<std::shared_mutex> lock(shard_locks_[shard_idx]);
        
        // 批量删除
        for (const auto& key : group) {
            if (shards_[shard_idx]->del(key)) {
                deleted_count++;
            }
        }
    }
    
    return deleted_count;
}

size_t ShardedKVStore::size() const {
    size_t total_size = 0;
    
    // 获取所有分片的读锁
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(shard_count_);
    
    for (size_t i = 0; i < shard_count_; ++i) {
        locks.emplace_back(shard_locks_[i]);
    }
    
    // 计算总大小
    for (size_t i = 0; i < shard_count_; ++i) {
        total_size += shards_[i]->size();
    }
    
    return total_size;
}

std::vector<size_t> ShardedKVStore::shard_sizes() const {
    std::vector<size_t> sizes;
    sizes.reserve(shard_count_);
    
    // 获取所有分片的读锁
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(shard_count_);
    
    for (size_t i = 0; i < shard_count_; ++i) {
        locks.emplace_back(shard_locks_[i]);
    }
    
    // 获取每个分片的大小
    for (size_t i = 0; i < shard_count_; ++i) {
        sizes.push_back(shards_[i]->size());
    }
    
    return sizes;
}

void ShardedKVStore::clear() {
    // 获取所有分片的写锁
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(shard_count_);
    
    for (size_t i = 0; i < shard_count_; ++i) {
        locks.emplace_back(shard_locks_[i]);
    }
    
    // 清空所有分片
    for (size_t i = 0; i < shard_count_; ++i) {
        shards_[i]->clear();
    }
}

void ShardedKVStore::cleanup_expired() {
    // 获取所有分片的写锁
    std::vector<std::unique_lock<std::shared_mutex>> locks;
    locks.reserve(shard_count_);
    
    for (size_t i = 0; i < shard_count_; ++i) {
        locks.emplace_back(shard_locks_[i]);
    }
    
    // 清理所有分片的过期键
    for (size_t i = 0; i < shard_count_; ++i) {
        shards_[i]->cleanupExpired();
    }
}

std::shared_ptr<StorageEngine> ShardedKVStore::get_shard(size_t index) const {
    if (index >= shard_count_) {
        return nullptr;
    }
    
    return shards_[index];
}
