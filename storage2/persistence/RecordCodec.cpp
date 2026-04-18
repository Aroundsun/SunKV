#include "RecordCodec.h"

#include <cstddef>
#include <cstdint>

#include "DataValueCodec.h"

namespace sunkv::storage2 {

namespace {
static void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
static void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}
static void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}
static bool readU8(const uint8_t* data, size_t len, size_t* off, uint8_t* out) {
    if (*off + 1 > len) return false;
    *out = data[*off];
    *off += 1;
    return true;
}
static bool readU32(const uint8_t* data, size_t len, size_t* off, uint32_t* out) {
    if (*off + 4 > len) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(data[*off + i]) << (i * 8));
    *off += 4;
    *out = v;
    return true;
}
static bool readU64(const uint8_t* data, size_t len, size_t* off, uint64_t* out) {
    if (*off + 8 > len) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(data[*off + i]) << (i * 8));
    *off += 8;
    *out = v;
    return true;
}
} // namespace

std::vector<uint8_t> RecordCodec::encode(const Record& r) {
    // 格式：
    // magic(2)='R''C'
    // version(1)
    // record_version(u64)
    // dv_len(u32)
    // dv_bytes(DataValueCodec::encode, 内含 expire_at_us)
    std::vector<uint8_t> out;
    appendU8(out, static_cast<uint8_t>('R'));
    appendU8(out, static_cast<uint8_t>('C'));
    appendU8(out, kVersion);
    appendU64(out, r.version);

    auto dv = DataValueCodec::encode(r.value, r.expire_at_us);
    appendU32(out, static_cast<uint32_t>(dv.size()));
    out.insert(out.end(), dv.begin(), dv.end());
    return out;
}

bool RecordCodec::decode(const uint8_t* data, size_t len, Record* out) {
    if (!data || !out) return false;
    size_t off = 0;
    uint8_t b0 = 0, b1 = 0, ver = 0;
    if (!readU8(data, len, &off, &b0) || !readU8(data, len, &off, &b1)) return false;
    if (b0 != static_cast<uint8_t>('R') || b1 != static_cast<uint8_t>('C')) return false;
    if (!readU8(data, len, &off, &ver)) return false;
    if (ver != kVersion) return false;

    uint64_t record_ver = 0;
    if (!readU64(data, len, &off, &record_ver)) return false;

    uint32_t dv_len = 0;
    if (!readU32(data, len, &off, &dv_len)) return false;
    if (off + dv_len > len) return false;

    DataValue dv_value;
    int64_t expire_at_us = -1;
    if (!DataValueCodec::decode(data + off, dv_len, &dv_value, &expire_at_us)) return false;

    out->value = std::move(dv_value);
    out->expire_at_us = expire_at_us;
    out->version = record_ver;
    return true;
}

} // namespace sunkv::storage2

