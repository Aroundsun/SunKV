#include "OrchestratedStorageAPI.h"

namespace sunkv::storage2 {

OrchestratedStorageAPI::OrchestratedStorageAPI(std::unique_ptr<IStorageAPI> inner, PersistenceOrchestrator* orchestrator)
    : inner_(std::move(inner)), orchestrator_(orchestrator) {}

StorageResult<bool> OrchestratedStorageAPI::set(const std::string& key, const std::string& value) {
    return submitAndStrip(inner_->set(key, value));
}
StorageResult<std::optional<std::string>> OrchestratedStorageAPI::get(const std::string& key) {
    return submitAndStrip(inner_->get(key));
}
StorageResult<int64_t> OrchestratedStorageAPI::del(const std::string& key) { return submitAndStrip(inner_->del(key)); }
StorageResult<int64_t> OrchestratedStorageAPI::exists(const std::string& key) { return submitAndStrip(inner_->exists(key)); }

StorageResult<int64_t> OrchestratedStorageAPI::expire(const std::string& key, int64_t ttl_seconds) {
    return submitAndStrip(inner_->expire(key, ttl_seconds));
}
StorageResult<int64_t> OrchestratedStorageAPI::persist(const std::string& key) { return submitAndStrip(inner_->persist(key)); }
StorageResult<int64_t> OrchestratedStorageAPI::ttl(const std::string& key) { return submitAndStrip(inner_->ttl(key)); }
StorageResult<int64_t> OrchestratedStorageAPI::pttl(const std::string& key) { return submitAndStrip(inner_->pttl(key)); }

StorageResult<int64_t> OrchestratedStorageAPI::dbsize() { return submitAndStrip(inner_->dbsize()); }
StorageResult<std::vector<std::string>> OrchestratedStorageAPI::keys() { return submitAndStrip(inner_->keys()); }

StorageResult<int64_t> OrchestratedStorageAPI::lpush(const std::string& key, const std::vector<std::string>& values) {
    return submitAndStrip(inner_->lpush(key, values));
}
StorageResult<int64_t> OrchestratedStorageAPI::rpush(const std::string& key, const std::vector<std::string>& values) {
    return submitAndStrip(inner_->rpush(key, values));
}
StorageResult<std::optional<std::string>> OrchestratedStorageAPI::lpop(const std::string& key) {
    return submitAndStrip(inner_->lpop(key));
}
StorageResult<std::optional<std::string>> OrchestratedStorageAPI::rpop(const std::string& key) {
    return submitAndStrip(inner_->rpop(key));
}
StorageResult<int64_t> OrchestratedStorageAPI::llen(const std::string& key) { return submitAndStrip(inner_->llen(key)); }
StorageResult<std::optional<std::string>> OrchestratedStorageAPI::lindex(const std::string& key, int64_t index) {
    return submitAndStrip(inner_->lindex(key, index));
}

StorageResult<int64_t> OrchestratedStorageAPI::sadd(const std::string& key, const std::vector<std::string>& members) {
    return submitAndStrip(inner_->sadd(key, members));
}
StorageResult<int64_t> OrchestratedStorageAPI::srem(const std::string& key, const std::vector<std::string>& members) {
    return submitAndStrip(inner_->srem(key, members));
}
StorageResult<int64_t> OrchestratedStorageAPI::scard(const std::string& key) { return submitAndStrip(inner_->scard(key)); }
StorageResult<int64_t> OrchestratedStorageAPI::sismember(const std::string& key, const std::string& member) {
    return submitAndStrip(inner_->sismember(key, member));
}
StorageResult<std::vector<std::string>> OrchestratedStorageAPI::smembers(const std::string& key) {
    return submitAndStrip(inner_->smembers(key));
}

StorageResult<int64_t> OrchestratedStorageAPI::hset(const std::string& key, const std::string& field, const std::string& value) {
    return submitAndStrip(inner_->hset(key, field, value));
}
StorageResult<std::optional<std::string>> OrchestratedStorageAPI::hget(const std::string& key, const std::string& field) {
    return submitAndStrip(inner_->hget(key, field));
}
StorageResult<int64_t> OrchestratedStorageAPI::hdel(const std::string& key, const std::vector<std::string>& fields) {
    return submitAndStrip(inner_->hdel(key, fields));
}
StorageResult<int64_t> OrchestratedStorageAPI::hlen(const std::string& key) { return submitAndStrip(inner_->hlen(key)); }
StorageResult<int64_t> OrchestratedStorageAPI::hexists(const std::string& key, const std::string& field) {
    return submitAndStrip(inner_->hexists(key, field));
}
StorageResult<std::vector<std::pair<std::string, std::string>>> OrchestratedStorageAPI::hgetall(const std::string& key) {
    return submitAndStrip(inner_->hgetall(key));
}

} // namespace sunkv::storage2

