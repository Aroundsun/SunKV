#include "StorageEngine.h"

#include "../backend/IBackend.h"
#include "Time.h"

namespace sunkv::storage2 {

namespace {

struct BypassStorageLimitGuard {
    IBackend* b{nullptr};
    explicit BypassStorageLimitGuard(IBackend& backend) : b(&backend) { b->setBypassStorageLimit(true); }
    ~BypassStorageLimitGuard() {
        if (b) {
            b->setBypassStorageLimit(false);
        }
    }
};

} // namespace

StorageEngine::StorageEngine(std::unique_ptr<IBackend> backend) : backend_(std::move(backend)) {}

void StorageEngine::loadSnapshot(const std::vector<std::pair<std::string, Record>>& records) {
    BypassStorageLimitGuard guard(*backend_);
    backend_->clearAll();
    for (const auto& kv : records) {
        if (!backend_->putRecord(kv.first, kv.second)) {
            backend_->clearAll();
            return;
        }
    }
}

bool StorageEngine::applyMutation(const Mutation& m) {
    BypassStorageLimitGuard guard(*backend_);
    if (m.type == MutationType::PutRecord) {
        if (!m.record.has_value()) return false;
        if (!backend_->putRecord(m.key, *m.record)) return false;
        return true;
    }
    if (m.type == MutationType::DelKey) {
        (void)backend_->delKey(m.key);
        return true;
    }
    if (m.type == MutationType::ClearAll) {
        backend_->clearAll();
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, Record>> StorageEngine::dumpAllLiveRecords() {
    std::vector<std::pair<std::string, Record>> out;
    auto ks = backend_->keys();
    out.reserve(ks.size());
    MutationBatch muts;
    for (const auto& k : ks) {
        muts.clear();
        auto r = getLiveRecordOrExpire(k, &muts);
        if (r.has_value()) {
            out.push_back({k, *r});
        }
        // 若发现过期会惰性删除；dump 不需要向外暴露 mutations
    }
    return out;
}

bool StorageEngine::isExpired(const Record& r, int64_t now_us) const {
    return r.expire_at_us >= 0 && now_us >= r.expire_at_us;
}

StorageResult<bool> StorageEngine::set(const std::string& key, const std::string& value) {
    Record r;
    r.value = DataValue(value);
    r.expire_at_us = -1;
    r.version = 0;

    if (!backend_->putRecord(key, r)) {
        return StorageResult<bool>::err(StatusCode::QuotaExceeded);
    }

    Mutation m;
    m.type = MutationType::PutRecord;
    m.key = key;
    m.record = r;
    m.ts_us = nowEpochUs();
    return StorageResult<bool>::ok(true, {m});
}

StorageResult<std::optional<std::string>> StorageEngine::get(const std::string& key) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<std::optional<std::string>>::ok(std::nullopt, {m});
    }
    if (r->value.type != DataType::STRING) {
        return StorageResult<std::optional<std::string>>::err(StatusCode::WrongType);
    }
    return StorageResult<std::optional<std::string>>::ok(r->value.string_value);
}

StorageResult<int64_t> StorageEngine::del(const std::string& key) {
    bool removed = backend_->delKey(key);
    if (!removed) {
        return StorageResult<int64_t>::ok(0);
    }
    Mutation m;
    m.type = MutationType::DelKey;
    m.key = key;
    m.ts_us = nowEpochUs();
    return StorageResult<int64_t>::ok(1, {m});
}

StorageResult<int64_t> StorageEngine::exists(const std::string& key) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(0, {m});
    }
    return StorageResult<int64_t>::ok(1);
}

StorageResult<int64_t> StorageEngine::expire(const std::string& key, int64_t ttl_seconds) {
    if (ttl_seconds < 0) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(0, {m});
    }
    if (ttl_seconds == 0) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(1, {m});
    }

    r->expire_at_us = now + ttl_seconds * 1000'000;
    if (!backend_->putRecord(key, *r)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }
    Mutation m;
    m.type = MutationType::PutRecord;
    m.key = key;
    m.record = *r;
    m.ts_us = now;
    return StorageResult<int64_t>::ok(1, {m});
}

StorageResult<int64_t> StorageEngine::persist(const std::string& key) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(0, {m});
    }
    bool had_ttl = r->expire_at_us >= 0;
    r->expire_at_us = -1;
    if (!backend_->putRecord(key, *r)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }
    Mutation m;
    m.type = MutationType::PutRecord;
    m.key = key;
    m.record = *r;
    m.ts_us = now;
    return StorageResult<int64_t>::ok(had_ttl ? 1 : 0, {m});
}

StorageResult<int64_t> StorageEngine::ttl(const std::string& key) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(-2);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(-2, {m});
    }
    if (r->expire_at_us < 0) {
        return StorageResult<int64_t>::ok(-1);
    }
    int64_t remain_us = r->expire_at_us - now;
    if (remain_us <= 0) {
        // 与 get/exists 的惰性过期行为保持一致：真正到期时删除并返回 -2。
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(-2, {m});
    }
    int64_t remain_s = remain_us / 1000'000; // 向下取整，(0,1s) 返回 0
    return StorageResult<int64_t>::ok(remain_s);
}

StorageResult<int64_t> StorageEngine::pttl(const std::string& key) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(-2);
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(-2, {m});
    }
    if (r->expire_at_us < 0) {
        return StorageResult<int64_t>::ok(-1);
    }
    int64_t remain_us = r->expire_at_us - now;
    if (remain_us <= 0) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = now;
        return StorageResult<int64_t>::ok(-2, {m});
    }
    int64_t remain_ms = remain_us / 1000; // 向下取整，(0,1ms) 返回 0
    return StorageResult<int64_t>::ok(remain_ms);
}

StorageResult<int64_t> StorageEngine::dbsize() {
    return StorageResult<int64_t>::ok(backend_->size());
}

StorageResult<std::vector<std::string>> StorageEngine::keys() {
    // v0.1：返回后端所有 key（不做 scan，不过滤过期；过期会在访问时惰性删除）
    return StorageResult<std::vector<std::string>>::ok(backend_->keys());
}

std::optional<Record> StorageEngine::getLiveRecordOrExpire(const std::string& key, MutationBatch* mutations) {
    auto r = backend_->getRecord(key);
    if (!r.has_value()) {
        return std::nullopt;
    }
    const int64_t now = nowEpochUs();
    if (isExpired(*r, now)) {
        backend_->delKey(key);
        if (mutations) {
            Mutation m;
            m.type = MutationType::DelKey;
            m.key = key;
            m.ts_us = now;
            mutations->push_back(std::move(m));
        }
        return std::nullopt;
    }
    return r;
}

StorageResult<Record> StorageEngine::getOrInitContainer(const std::string& key, DataType want_type) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        Record nr;
        nr.value.type = want_type;
        nr.value.string_value.clear();
        nr.expire_at_us = -1;
        nr.version = 0;
        if (!backend_->putRecord(key, nr)) {
            return StorageResult<Record>::err(StatusCode::QuotaExceeded);
        }

        Mutation m;
        m.type = MutationType::PutRecord;
        m.key = key;
        m.record = nr;
        m.ts_us = nowEpochUs();
        muts.push_back(std::move(m));
        return StorageResult<Record>::ok(nr, std::move(muts));
    }

    // 只允许同类型容器；若 key 已存在且类型不匹配，返回 WRONGTYPE（与 Redis 语义一致）。
    if (r->value.type == want_type) {
        return StorageResult<Record>::ok(*r, std::move(muts));
    }
    return StorageResult<Record>::err(StatusCode::WrongType);
}

StorageResult<int64_t> StorageEngine::lpush(const std::string& key, const std::vector<std::string>& values) {
    if (values.empty()) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    auto r = getOrInitContainer(key, DataType::LIST);
    if (r.status != StatusCode::Ok) {
        return StorageResult<int64_t>::err(r.status);
    }
    for (const auto& v : values) {
        r.value.value.list_value.push_front(v);
    }
    if (!backend_->putRecord(key, r.value)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }

    Mutation m;
    m.type = MutationType::PutRecord;
    m.key = key;
    m.record = r.value;
    m.ts_us = nowEpochUs();
    r.mutations.push_back(std::move(m));

    return StorageResult<int64_t>::ok(static_cast<int64_t>(r.value.value.list_value.size()), std::move(r.mutations));
}

StorageResult<int64_t> StorageEngine::rpush(const std::string& key, const std::vector<std::string>& values) {
    if (values.empty()) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    auto r = getOrInitContainer(key, DataType::LIST);
    if (r.status != StatusCode::Ok) {
        return StorageResult<int64_t>::err(r.status);
    }
    for (const auto& v : values) {
        r.value.value.list_value.push_back(v);
    }
    if (!backend_->putRecord(key, r.value)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }

    Mutation m;
    m.type = MutationType::PutRecord;
    m.key = key;
    m.record = r.value;
    m.ts_us = nowEpochUs();
    r.mutations.push_back(std::move(m));

    return StorageResult<int64_t>::ok(static_cast<int64_t>(r.value.value.list_value.size()), std::move(r.mutations));
}

StorageResult<std::optional<std::string>> StorageEngine::lpop(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt, std::move(muts));
    }
    if (r->value.type != DataType::LIST) {
        return StorageResult<std::optional<std::string>>::err(StatusCode::WrongType);
    }
    if (r->value.list_value.empty()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }
    std::string elem = r->value.list_value.front();
    r->value.list_value.pop_front();
    if (r->value.list_value.empty()) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = nowEpochUs();
        muts.push_back(std::move(m));
    } else {
        if (!backend_->putRecord(key, *r)) {
            return StorageResult<std::optional<std::string>>::err(StatusCode::QuotaExceeded);
        }
        Mutation m;
        m.type = MutationType::PutRecord;
        m.key = key;
        m.record = *r;
        m.ts_us = nowEpochUs();
        muts.push_back(std::move(m));
    }

    return StorageResult<std::optional<std::string>>::ok(elem, std::move(muts));
}

StorageResult<std::optional<std::string>> StorageEngine::rpop(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt, std::move(muts));
    }
    if (r->value.type != DataType::LIST) {
        return StorageResult<std::optional<std::string>>::err(StatusCode::WrongType);
    }
    if (r->value.list_value.empty()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }
    std::string elem = r->value.list_value.back();
    r->value.list_value.pop_back();
    if (r->value.list_value.empty()) {
        backend_->delKey(key);
        Mutation m;
        m.type = MutationType::DelKey;
        m.key = key;
        m.ts_us = nowEpochUs();
        muts.push_back(std::move(m));
    } else {
        if (!backend_->putRecord(key, *r)) {
            return StorageResult<std::optional<std::string>>::err(StatusCode::QuotaExceeded);
        }
        Mutation m;
        m.type = MutationType::PutRecord;
        m.key = key;
        m.record = *r;
        m.ts_us = nowEpochUs();
        muts.push_back(std::move(m));
    }

    return StorageResult<std::optional<std::string>>::ok(elem, std::move(muts));
}

StorageResult<int64_t> StorageEngine::llen(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::LIST) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    return StorageResult<int64_t>::ok(static_cast<int64_t>(r->value.list_value.size()));
}

StorageResult<std::optional<std::string>> StorageEngine::lindex(const std::string& key, int64_t index) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt, std::move(muts));
    }
    if (r->value.type != DataType::LIST) {
        return StorageResult<std::optional<std::string>>::err(StatusCode::WrongType);
    }

    const int64_t n = static_cast<int64_t>(r->value.list_value.size());
    if (n == 0) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }
    // Redis 语义：支持负索引
    int64_t idx = index;
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }

    auto it = r->value.list_value.begin();
    std::advance(it, static_cast<size_t>(idx));
    return StorageResult<std::optional<std::string>>::ok(*it);
}

StorageResult<int64_t> StorageEngine::sadd(const std::string& key, const std::vector<std::string>& members) {
    if (members.empty()) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    auto r = getOrInitContainer(key, DataType::SET);
    if (r.status != StatusCode::Ok) {
        return StorageResult<int64_t>::err(r.status);
    }
    int64_t added = 0;
    for (const auto& m : members) {
        if (r.value.value.set_value.insert(m).second) {
            ++added;
        }
    }
    if (!backend_->putRecord(key, r.value)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }

    Mutation mu;
    mu.type = MutationType::PutRecord;
    mu.key = key;
    mu.record = r.value;
    mu.ts_us = nowEpochUs();
    r.mutations.push_back(std::move(mu));

    return StorageResult<int64_t>::ok(added, std::move(r.mutations));
}

StorageResult<int64_t> StorageEngine::srem(const std::string& key, const std::vector<std::string>& members) {
    if (members.empty()) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::SET) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    int64_t removed = 0;
    for (const auto& m : members) {
        removed += static_cast<int64_t>(r->value.set_value.erase(m));
    }
    if (r->value.set_value.empty()) {
        backend_->delKey(key);
        Mutation mu;
        mu.type = MutationType::DelKey;
        mu.key = key;
        mu.ts_us = nowEpochUs();
        muts.push_back(std::move(mu));
    } else {
        if (!backend_->putRecord(key, *r)) {
            return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
        }
        Mutation mu;
        mu.type = MutationType::PutRecord;
        mu.key = key;
        mu.record = *r;
        mu.ts_us = nowEpochUs();
        muts.push_back(std::move(mu));
    }

    return StorageResult<int64_t>::ok(removed, std::move(muts));
}

StorageResult<int64_t> StorageEngine::scard(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::SET) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    return StorageResult<int64_t>::ok(static_cast<int64_t>(r->value.set_value.size()));
}

StorageResult<int64_t> StorageEngine::sismember(const std::string& key, const std::string& member) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::SET) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    return StorageResult<int64_t>::ok(r->value.set_value.count(member) ? 1 : 0);
}

StorageResult<std::vector<std::string>> StorageEngine::smembers(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::vector<std::string>>::ok({}, std::move(muts));
    }
    if (r->value.type != DataType::SET) {
        return StorageResult<std::vector<std::string>>::err(StatusCode::WrongType);
    }
    std::vector<std::string> out(r->value.set_value.begin(), r->value.set_value.end());
    return StorageResult<std::vector<std::string>>::ok(std::move(out));
}

StorageResult<int64_t> StorageEngine::hset(const std::string& key, const std::string& field, const std::string& value) {
    auto r = getOrInitContainer(key, DataType::HASH);
    if (r.status != StatusCode::Ok) {
        return StorageResult<int64_t>::err(r.status);
    }
    bool is_new = r.value.value.hash_value.find(field) == r.value.value.hash_value.end();
    r.value.value.hash_value[field] = value;
    if (!backend_->putRecord(key, r.value)) {
        return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
    }

    Mutation mu;
    mu.type = MutationType::PutRecord;
    mu.key = key;
    mu.record = r.value;
    mu.ts_us = nowEpochUs();
    r.mutations.push_back(std::move(mu));

    return StorageResult<int64_t>::ok(is_new ? 1 : 0, std::move(r.mutations));
}

StorageResult<std::optional<std::string>> StorageEngine::hget(const std::string& key, const std::string& field) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt, std::move(muts));
    }
    if (r->value.type != DataType::HASH) {
        return StorageResult<std::optional<std::string>>::err(StatusCode::WrongType);
    }
    auto it = r->value.hash_value.find(field);
    if (it == r->value.hash_value.end()) {
        return StorageResult<std::optional<std::string>>::ok(std::nullopt);
    }
    return StorageResult<std::optional<std::string>>::ok(it->second);
}

StorageResult<int64_t> StorageEngine::hdel(const std::string& key, const std::vector<std::string>& fields) {
    if (fields.empty()) {
        return StorageResult<int64_t>::err(StatusCode::InvalidArg);
    }
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::HASH) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    int64_t deleted = 0;
    for (const auto& f : fields) {
        deleted += static_cast<int64_t>(r->value.hash_value.erase(f));
    }
    if (r->value.hash_value.empty()) {
        backend_->delKey(key);
        Mutation mu;
        mu.type = MutationType::DelKey;
        mu.key = key;
        mu.ts_us = nowEpochUs();
        muts.push_back(std::move(mu));
    } else {
        if (!backend_->putRecord(key, *r)) {
            return StorageResult<int64_t>::err(StatusCode::QuotaExceeded);
        }
        Mutation mu;
        mu.type = MutationType::PutRecord;
        mu.key = key;
        mu.record = *r;
        mu.ts_us = nowEpochUs();
        muts.push_back(std::move(mu));
    }

    return StorageResult<int64_t>::ok(deleted, std::move(muts));
}

StorageResult<int64_t> StorageEngine::hlen(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::HASH) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    return StorageResult<int64_t>::ok(static_cast<int64_t>(r->value.hash_value.size()));
}

StorageResult<int64_t> StorageEngine::hexists(const std::string& key, const std::string& field) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<int64_t>::ok(0, std::move(muts));
    }
    if (r->value.type != DataType::HASH) {
        return StorageResult<int64_t>::err(StatusCode::WrongType);
    }
    return StorageResult<int64_t>::ok(r->value.hash_value.count(field) ? 1 : 0);
}

StorageResult<std::vector<std::pair<std::string, std::string>>> StorageEngine::hgetall(const std::string& key) {
    MutationBatch muts;
    auto r = getLiveRecordOrExpire(key, &muts);
    if (!r.has_value()) {
        return StorageResult<std::vector<std::pair<std::string, std::string>>>::ok({}, std::move(muts));
    }
    if (r->value.type != DataType::HASH) {
        return StorageResult<std::vector<std::pair<std::string, std::string>>>::err(StatusCode::WrongType);
    }
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(r->value.hash_value.size());
    for (const auto& kv : r->value.hash_value) {
        out.push_back(kv);
    }
    return StorageResult<std::vector<std::pair<std::string, std::string>>>::ok(std::move(out));
}

} // namespace sunkv::storage2

