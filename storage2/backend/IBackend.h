#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../engine/Mutation.h"

namespace sunkv::storage2 {

class IBackend {
public:
    virtual ~IBackend() = default;

    virtual std::optional<Record> getRecord(const std::string& key) = 0;
    virtual void putRecord(const std::string& key, const Record& record) = 0;
    virtual bool delKey(const std::string& key) = 0;
    virtual void clearAll() = 0;
    virtual int64_t size() = 0;
    virtual std::vector<std::string> keys() = 0;
};

} // namespace sunkv::storage2

