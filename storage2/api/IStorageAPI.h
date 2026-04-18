#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "StorageResult.h"
 
namespace sunkv::storage2 {

// v2 存储对外 API（命令语义层）：不认识 RESP，不负责持久化 IO。
class IStorageAPI {
public:
    virtual ~IStorageAPI() = default;

    // string
    virtual StorageResult<bool> set(const std::string& key, const std::string& value) = 0;
    virtual StorageResult<std::optional<std::string>> get(const std::string& key) = 0;
    virtual StorageResult<int64_t> del(const std::string& key) = 0;
    virtual StorageResult<int64_t> exists(const std::string& key) = 0;

    // TTL
    virtual StorageResult<int64_t> expire(const std::string& key, int64_t ttl_seconds) = 0;
    virtual StorageResult<int64_t> persist(const std::string& key) = 0;
    virtual StorageResult<int64_t> ttl(const std::string& key) = 0;
    virtual StorageResult<int64_t> pttl(const std::string& key) = 0;

    // 管理接口（给快照/恢复/工具用）
    virtual StorageResult<int64_t> dbsize() = 0;
    virtual StorageResult<std::vector<std::string>> keys() = 0;

    // list
    virtual StorageResult<int64_t> lpush(const std::string& key, const std::vector<std::string>& values) = 0;
    virtual StorageResult<int64_t> rpush(const std::string& key, const std::vector<std::string>& values) = 0;
    virtual StorageResult<std::optional<std::string>> lpop(const std::string& key) = 0;
    virtual StorageResult<std::optional<std::string>> rpop(const std::string& key) = 0;
    virtual StorageResult<int64_t> llen(const std::string& key) = 0;
    virtual StorageResult<std::optional<std::string>> lindex(const std::string& key, int64_t index) = 0;

    // set
    virtual StorageResult<int64_t> sadd(const std::string& key, const std::vector<std::string>& members) = 0;
    virtual StorageResult<int64_t> srem(const std::string& key, const std::vector<std::string>& members) = 0;
    virtual StorageResult<int64_t> scard(const std::string& key) = 0;
    virtual StorageResult<int64_t> sismember(const std::string& key, const std::string& member) = 0;
    virtual StorageResult<std::vector<std::string>> smembers(const std::string& key) = 0;

    // hash
    virtual StorageResult<int64_t> hset(const std::string& key, const std::string& field, const std::string& value) = 0;
    virtual StorageResult<std::optional<std::string>> hget(const std::string& key, const std::string& field) = 0;
    virtual StorageResult<int64_t> hdel(const std::string& key, const std::vector<std::string>& fields) = 0;
    virtual StorageResult<int64_t> hlen(const std::string& key) = 0;
    virtual StorageResult<int64_t> hexists(const std::string& key, const std::string& field) = 0;
    virtual StorageResult<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key) = 0;
};

} // namespace sunkv::storage2

