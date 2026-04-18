#pragma once

#include <cstdint>
#include <vector>

#include "../model/DataValue.h"

namespace sunkv::storage2 {

// storage2 内聚的 DataValue 二进制编解码器，避免依赖旧 storage 模块。
class DataValueCodec {
public:
    static constexpr uint8_t kVersion = 1;

    static std::vector<uint8_t> encode(const DataValue& v, int64_t expire_at_epoch_us);
    static bool decode(const uint8_t* data, size_t len, DataValue* out, int64_t* expire_at_epoch_us);
    static bool decode(const std::vector<uint8_t>& bytes, DataValue* out, int64_t* expire_at_epoch_us) {
        return decode(bytes.data(), bytes.size(), out, expire_at_epoch_us);
    }
};

} // namespace sunkv::storage2

