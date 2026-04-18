#pragma once

#include <memory>

#include "../api/IStorageAPI.h"
#include "../persistence/PersistenceOrchestrator.h"

namespace sunkv::storage2 {

// 自动持久化装饰器：
// - 将 inner 返回的 mutations 自动 submit 给 orchestrator
// - 返回给上层的 StorageResult 中 mutations 会被清空（避免重复提交）
class OrchestratedStorageAPI final : public IStorageAPI {
public:
    OrchestratedStorageAPI(std::unique_ptr<IStorageAPI> inner, PersistenceOrchestrator* orchestrator);

    StorageResult<bool> set(const std::string& key, const std::string& value) override;
    StorageResult<std::optional<std::string>> get(const std::string& key) override;
    StorageResult<int64_t> del(const std::string& key) override;
    StorageResult<int64_t> exists(const std::string& key) override;

    StorageResult<int64_t> expire(const std::string& key, int64_t ttl_seconds) override;
    StorageResult<int64_t> persist(const std::string& key) override;
    StorageResult<int64_t> ttl(const std::string& key) override;
    StorageResult<int64_t> pttl(const std::string& key) override;

    StorageResult<int64_t> dbsize() override;
    StorageResult<std::vector<std::string>> keys() override;

    StorageResult<int64_t> lpush(const std::string& key, const std::vector<std::string>& values) override;
    StorageResult<int64_t> rpush(const std::string& key, const std::vector<std::string>& values) override;
    StorageResult<std::optional<std::string>> lpop(const std::string& key) override;
    StorageResult<std::optional<std::string>> rpop(const std::string& key) override;
    StorageResult<int64_t> llen(const std::string& key) override;
    StorageResult<std::optional<std::string>> lindex(const std::string& key, int64_t index) override;

    StorageResult<int64_t> sadd(const std::string& key, const std::vector<std::string>& members) override;
    StorageResult<int64_t> srem(const std::string& key, const std::vector<std::string>& members) override;
    StorageResult<int64_t> scard(const std::string& key) override;
    StorageResult<int64_t> sismember(const std::string& key, const std::string& member) override;
    StorageResult<std::vector<std::string>> smembers(const std::string& key) override;

    StorageResult<int64_t> hset(const std::string& key, const std::string& field, const std::string& value) override;
    StorageResult<std::optional<std::string>> hget(const std::string& key, const std::string& field) override;
    StorageResult<int64_t> hdel(const std::string& key, const std::vector<std::string>& fields) override;
    StorageResult<int64_t> hlen(const std::string& key) override;
    StorageResult<int64_t> hexists(const std::string& key, const std::string& field) override;
    StorageResult<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key) override;

private:
    template <class T>
    StorageResult<T> submitAndStrip(StorageResult<T> r) {
        if (orchestrator_ && !r.mutations.empty()) {
            orchestrator_->submit(std::move(r.mutations));
        }
        r.mutations.clear();
        return r;
    }

    std::unique_ptr<IStorageAPI> inner_;
    PersistenceOrchestrator* orchestrator_{nullptr}; // non-owning
};

} // namespace sunkv::storage2

