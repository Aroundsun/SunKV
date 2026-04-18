#include "InMemoryBackend.h"

namespace {
template <class T>
int64_t to_i64(T v) {
    return static_cast<int64_t>(v);
}
} // namespace

namespace sunkv::storage2 {

std::optional<Record> InMemoryBackend::getRecord(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void InMemoryBackend::putRecord(const std::string& key, const Record& record) {
    std::lock_guard<std::mutex> lock(mu_);
    map_[key] = record;
}

bool InMemoryBackend::delKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    return map_.erase(key) > 0;
}

void InMemoryBackend::clearAll() {
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear();
}

int64_t InMemoryBackend::size() {
    std::lock_guard<std::mutex> lock(mu_);
    return to_i64(map_.size());
}

std::vector<std::string> InMemoryBackend::keys() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::string> ks;
    ks.reserve(map_.size());
    for (const auto& kv : map_) {
        ks.push_back(kv.first);
    }
    return ks;
}

} // namespace sunkv::storage2

