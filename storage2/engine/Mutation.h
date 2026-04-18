#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../model/DataValue.h"

namespace sunkv::storage2 {

    struct Record {
        DataValue value;
        int64_t expire_at_us{-1}; // -1 表示无 TTL
        uint64_t version{};
    };

    enum class MutationType {
        PutRecord = 0,
        DelKey = 1,
        ClearAll = 2,
    };

    struct Mutation {
        MutationType type{MutationType::PutRecord};
        std::string key;
        std::optional<Record> record; // PutRecord 必填
        int64_t ts_us{0};
    };

} // namespace sunkv::storage2

