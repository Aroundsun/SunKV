#pragma once

#include <cstdint>
#include <vector>

#include "../engine/Mutation.h"

namespace sunkv::storage2 {

class RecordCodec {
public:
    static constexpr uint8_t kVersion = 1;
    // 编码 Record 为二进制
    static std::vector<uint8_t> encode(const Record& r);
    // 解码二进制为 Record
    static bool decode(const uint8_t* data, size_t len, Record* out);
    // 解码二进制为 Record
    static bool decode(const std::vector<uint8_t>& bytes, Record* out) { return decode(bytes.data(), bytes.size(), out); }
};

} // namespace sunkv::storage2

