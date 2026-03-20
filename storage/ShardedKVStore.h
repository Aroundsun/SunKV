#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include "StorageEngine.h"

// 分片键值对存储
class ShardedKVStore {
public:
    explicit ShardedKVStore(size_t shard_count = 16);
    ~ShardedKVStore() = default;
    
    // 基础操作
    bool set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    std::string get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    
    // 批量操作
    std::vector<std::string> mget(const std::vector<std::string>& keys);
    bool mset(const std::vector<std::pair<std::string, std::string>>& key_values);
    int mdel(const std::vector<std::string>& keys);
    
    // 统计信息
    size_t size() const;
    size_t shard_count() const { return shard_count_; }
    std::vector<size_t> shard_sizes() const;
    
    // 清理操作
    void clear();
    void cleanup_expired();
    
    // 分片信息
    size_t get_shard_index(const std::string& key) const;
    std::shared_ptr<StorageEngine> get_shard(size_t index) const;

private:
    // 分片策略：使用 FNV-1a 哈希算法
    static uint32_t hash_key(const std::string& key);
    
    // 分片锁管理
    std::unique_ptr<std::shared_mutex[]> shard_locks_;
    
    // 存储引擎分片
    std::vector<std::shared_ptr<StorageEngine>> shards_;
    
    // 分片数量
    size_t shard_count_;
    
    // 禁止拷贝
    ShardedKVStore(const ShardedKVStore&) = delete;
    ShardedKVStore& operator=(const ShardedKVStore&) = delete;
};
