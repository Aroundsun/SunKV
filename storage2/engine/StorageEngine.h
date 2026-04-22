#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../api/IStorageAPI.h"
#include "../backend/IBackend.h"

namespace sunkv::storage2 {

// v2 StorageEngine：实现命令语义
class StorageEngine final : public IStorageAPI {
public:
    explicit StorageEngine(std::unique_ptr<IBackend> backend);

    // 给恢复/持久化层的“引擎级入口”：避免 persistence 直接写 backend。
    // v0.1：只保证 PutRecord/DelKey/ClearAll 可用（命令语义仍走 IStorageAPI）。
    void loadSnapshot(const std::vector<std::pair<std::string, Record>>& records);
    bool applyMutation(const Mutation& m);
    std::vector<std::pair<std::string, Record>> dumpAllLiveRecords();

    // string
    StorageResult<bool> set(const std::string& key, const std::string& value) override;
    StorageResult<std::optional<std::string>> get(const std::string& key) override;
    StorageResult<int64_t> del(const std::string& key) override;
    StorageResult<int64_t> exists(const std::string& key) override;
    // ttl
    StorageResult<int64_t> expire(const std::string& key, int64_t ttl_seconds) override;
    StorageResult<int64_t> persist(const std::string& key) override;
    StorageResult<int64_t> ttl(const std::string& key) override;
    StorageResult<int64_t> pttl(const std::string& key) override;

    StorageResult<int64_t> dbsize() override;
    StorageResult<std::vector<std::string>> keys() override;

    // list
    StorageResult<int64_t> lpush(const std::string& key, const std::vector<std::string>& values) override;
    StorageResult<int64_t> rpush(const std::string& key, const std::vector<std::string>& values) override;
    StorageResult<std::optional<std::string>> lpop(const std::string& key) override;
    StorageResult<std::optional<std::string>> rpop(const std::string& key) override;
    StorageResult<int64_t> llen(const std::string& key) override;
    StorageResult<std::optional<std::string>> lindex(const std::string& key, int64_t index) override;
    // set
    StorageResult<int64_t> sadd(const std::string& key, const std::vector<std::string>& members) override;
    StorageResult<int64_t> srem(const std::string& key, const std::vector<std::string>& members) override;
    StorageResult<int64_t> scard(const std::string& key) override;
    StorageResult<int64_t> sismember(const std::string& key, const std::string& member) override;
    StorageResult<std::vector<std::string>> smembers(const std::string& key) override;
    // hash
    StorageResult<int64_t> hset(const std::string& key, const std::string& field, const std::string& value) override;
    StorageResult<std::optional<std::string>> hget(const std::string& key, const std::string& field) override;
    StorageResult<int64_t> hdel(const std::string& key, const std::vector<std::string>& fields) override;
    StorageResult<int64_t> hlen(const std::string& key) override;
    StorageResult<int64_t> hexists(const std::string& key, const std::string& field) override;
    StorageResult<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key) override;

private:
    bool isExpired(const Record& r, int64_t now_us) const;
    std::optional<Record> getLiveRecordOrExpire(const std::string& key, MutationBatch* mutations);
    StorageResult<Record> getOrInitContainer(const std::string& key, DataType want_type);
    std::unique_ptr<IBackend> backend_;
};

} // namespace sunkv::storage2

