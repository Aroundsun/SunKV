#pragma once

#include <cstdint>
#include <vector>

#include "../model/DataValue.h"

namespace sunkv::storage2 {

//  DataValue 二进制编解码器 主要用于 RecordCodec 中
class DataValueCodec {
public:
    static constexpr uint8_t kVersion = 1;
    // 编码 DataValue 为二进制
    static std::vector<uint8_t> encode(const DataValue& v, int64_t expire_at_epoch_us);
    // 解码二进制为 DataValue
    static bool decode(const uint8_t* data, size_t len, DataValue* out, int64_t* expire_at_epoch_us);
    // 解码二进制为 DataValue
    static bool decode(const std::vector<uint8_t>& bytes, DataValue* out, int64_t* expire_at_epoch_us) {
        return decode(bytes.data(), bytes.size(), out, expire_at_epoch_us);
    }
};

} // namespace sunkv::storage2

