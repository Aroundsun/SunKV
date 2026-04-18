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
    std::optional<Record> getRecord(const std::string& key) override;
    void putRecord(const std::string& key, const Record& record) override;
    bool delKey(const std::string& key) override;
    void clearAll() override;
    int64_t size() override;
    std::vector<std::string> keys() override;

private:
    std::mutex mu_;
    std::unordered_map<std::string, Record> map_;
};

} // namespace sunkv::storage2

