#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "IBackend.h"

namespace sunkv::storage2 {

class InMemoryBackend final : public IBackend {
public:
    /// max_storage_bytes==0 表示不限制（仅估算内存占用作配额判断）
    explicit InMemoryBackend(size_t max_storage_bytes = 0);

    std::optional<Record> getRecord(const std::string& key) override;
    bool putRecord(const std::string& key, const Record& record) override;
    bool delKey(const std::string& key) override;
    void clearAll() override;
    int64_t size() override;
    std::vector<std::string> keys() override;

    void setBypassStorageLimit(bool on) override;

    size_t estimatedBytesUsed() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, Record> map_;
    size_t max_storage_bytes_{0};
    size_t bytes_used_{0};
    bool bypass_limit_{false};
};

} // namespace sunkv::storage2
