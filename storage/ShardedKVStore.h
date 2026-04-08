/**
 * @file ShardedKVStore.h
 * @brief SunKV 分片键值存储实现
 * 
 * 该文件实现分片存储引擎，提供：
 * - 水平分片以提升并发能力
 * - 基于 FNV-1a 的键分布哈希
 * - 细粒度分片锁以降低竞争
 * - 批量操作能力提升吞吐
 * - 分片级统计与管理接口
 */

#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include "StorageEngine.h"

/**
 * @class ShardedKVStore
 * @brief 线程安全的分片键值存储
 * 
 * 该类通过一致性哈希将键分布到多个存储分片，
 * 以减少全局锁竞争并提高并发读写性能。
 */
class ShardedKVStore {
public:
    /**
     * @brief 构造函数
     * @param shard_count 分片数量（默认 16）
     */
    explicit ShardedKVStore(size_t shard_count = 16);
    
    /**
     * @brief 析构函数
     */
    ~ShardedKVStore() = default;
    
    /**
     * @brief 设置键值对，并可选设置 TTL
     * @param key 要写入的键
     * @param value 要写入的值
     * @param ttl_ms 生存时间（毫秒），-1 表示不过期
     * @return 成功返回 true，失败返回 false
     */
    bool set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    /**
     * @brief 获取键对应的值
     * @param key 要查询的键
     * @return 若存在返回值，否则返回空字符串
     */
    std::string get(const std::string& key);
    
    /**
     * @brief 删除键
     * @param key 要删除的键
     * @return 删除成功返回 true，未找到返回 false
     */
    bool del(const std::string& key);
    
    /**
     * @brief 检查键是否存在
     * @param key 要检查的键
     * @return 存在返回 true，否则返回 false
     */
    bool exists(const std::string& key);
    
    /**
     * @brief 批量获取多个键的值
     * @param keys 待查询的键列表
     * @return 与输入键顺序对应的值列表
     */
    std::vector<std::string> mget(const std::vector<std::string>& keys);
    
    /**
     * @brief 批量设置键值对
     * @param key_values 待写入的键值对列表
     * @return 全部写入成功返回 true，否则返回 false
     */
    bool mset(const std::vector<std::pair<std::string, std::string>>& key_values);
    
    /**
     * @brief 批量删除键
     * @param keys 待删除的键列表
     * @return 成功删除的键数量
     */
    int mdel(const std::vector<std::string>& keys);
    
    /**
     * @brief 获取所有分片中的总条目数
     * @return 总条目数
     */
    size_t size() const;
    
    /**
     * @brief 获取分片数量
     * @return 分片数
     */
    size_t shard_count() const { return shard_count_; }
    
    /**
     * @brief 获取每个分片的条目数
     * @return 各分片条目数列表
     */
    std::vector<size_t> shard_sizes() const;
    
    /**
     * @brief 清空所有分片数据
     */
    void clear();
    
    /**
     * @brief 清理所有分片中的过期键
     */
    void cleanup_expired();
    
    /**
     * @brief 获取键所属的分片索引
     * @param key 待哈希的键
     * @return 分片索引
     */
    size_t get_shard_index(const std::string& key) const;
    
    /**
     * @brief 获取指定分片实例
     * @param index 分片索引
     * @return 对应分片存储引擎的共享指针
     */
    std::shared_ptr<StorageEngine> get_shard(size_t index) const;

private:
    /**
     * @brief 使用 FNV-1a 算法计算键哈希
     * @param key 待哈希的键
     * @return 哈希值
     */
    static uint32_t hash_key(const std::string& key);
    
    std::unique_ptr<std::shared_mutex[]> shard_locks_;          ///< 每个分片对应一把读写锁
    std::vector<std::shared_ptr<StorageEngine>> shards_;        ///< 分片存储引擎列表
    size_t shard_count_;                                         ///< 分片总数
    
    // 禁用拷贝语义，避免分片锁与分片实例被误复制
    ShardedKVStore(const ShardedKVStore&) = delete;
    ShardedKVStore& operator=(const ShardedKVStore&) = delete;
};
