#include "InMemoryBackend.h"

#include "../model/DataValue.h"

namespace {

size_t estimateDataValue(const DataValue& v) {
    size_t n = 32;
    switch (v.type) {
    case DataType::STRING:
        n += v.string_value.size();
        break;
    case DataType::LIST:
        for (const auto& s : v.list_value) {
            n += s.size();
        }
        break;
    case DataType::SET:
        for (const auto& s : v.set_value) {
            n += s.size();
        }
        break;
    case DataType::HASH:
        for (const auto& kv : v.hash_value.data) {
            n += kv.first.size() + kv.second.size();
        }
        break;
    }
    return n;
}

size_t estimateFootprint(const std::string& key, const sunkv::storage2::Record& r) {
    return key.size() + estimateDataValue(r.value) + sizeof(sunkv::storage2::Record) / 2;
}

} // namespace

namespace sunkv::storage2 {

InMemoryBackend::InMemoryBackend(size_t max_storage_bytes) : max_storage_bytes_(max_storage_bytes) {}

void InMemoryBackend::setBypassStorageLimit(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    bypass_limit_ = on;
}

size_t InMemoryBackend::estimatedBytesUsed() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bytes_used_;
}

std::optional<Record> InMemoryBackend::getRecord(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool InMemoryBackend::putRecord(const std::string& key, const Record& record) {
    std::lock_guard<std::mutex> lock(mu_);
    size_t old_fp = 0;
    auto it = map_.find(key);
    if (it != map_.end()) {
        old_fp = estimateFootprint(key, it->second);
    }
    const size_t new_fp = estimateFootprint(key, record);
    if (!bypass_limit_ && max_storage_bytes_ > 0) {
        const size_t projected = bytes_used_ - old_fp + new_fp;
        if (projected > max_storage_bytes_) {
            return false;
        }
    }
    bytes_used_ -= old_fp;
    map_[key] = record;
    bytes_used_ += new_fp;
    return true;
}

bool InMemoryBackend::delKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        return false;
    }
    bytes_used_ -= estimateFootprint(key, it->second);
    map_.erase(it);
    return true;
}

void InMemoryBackend::clearAll() {
    std::lock_guard<std::mutex> lock(mu_);
    map_.clear();
    bytes_used_ = 0;
}

int64_t InMemoryBackend::size() {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<int64_t>(map_.size());
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
