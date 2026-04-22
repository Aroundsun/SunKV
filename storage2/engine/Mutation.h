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
        PutRecord = 0, // key -> record（包含 value + expire_at）
        DelKey = 1, // 删除 key
        ClearAll = 2, // 清空
    };

    struct Mutation {
        MutationType type{MutationType::PutRecord}; // 写入事件类型
        std::string key; // 写入事件的 key
        std::optional<Record> record; // PutRecord 必填，记录包含 value + expire_at
        int64_t ts_us{0}; // 写入事件的时间
    };

} // namespace sunkv::storage2

