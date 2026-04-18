#include "DataValueCodec.h"

namespace sunkv::storage2 {
namespace {

static void appendU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
static void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}
static void appendI64(std::vector<uint8_t>& out, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
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
static bool readI64(const uint8_t* data, size_t len, size_t* off, int64_t* out) {
    if (*off + 8 > len) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(data[*off + i]) << (i * 8));
    *off += 8;
    *out = static_cast<int64_t>(v);
    return true;
}
static void appendBytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); }
static void appendString(std::vector<uint8_t>& out, const std::string& s) {
    appendU32(out, static_cast<uint32_t>(s.size()));
    appendBytes(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
static bool readString(const uint8_t* data, size_t len, size_t* off, std::string* out) {
    uint32_t n = 0;
    if (!readU32(data, len, off, &n)) return false;
    if (*off + n > len) return false;
    out->assign(reinterpret_cast<const char*>(data + *off), n);
    *off += n;
    return true;
}
} // namespace

std::vector<uint8_t> DataValueCodec::encode(const DataValue& v, int64_t expire_at_epoch_us) {
    std::vector<uint8_t> payload;
    switch (v.type) {
        case DataType::STRING:
            appendString(payload, v.string_value);
            break;
        case DataType::LIST:
            appendU32(payload, static_cast<uint32_t>(v.list_value.size()));
            for (const auto& item : v.list_value) appendString(payload, item);
            break;
        case DataType::SET:
            appendU32(payload, static_cast<uint32_t>(v.set_value.size()));
            for (const auto& item : v.set_value) appendString(payload, item);
            break;
        case DataType::HASH:
            appendU32(payload, static_cast<uint32_t>(v.hash_value.size()));
            for (const auto& kv : v.hash_value) {
                appendString(payload, kv.first);
                appendString(payload, kv.second);
            }
            break;
    }

    std::vector<uint8_t> out;
    out.reserve(2 + 1 + 1 + 1 + 1 + 8 + 4 + payload.size());
    appendU8(out, static_cast<uint8_t>('D'));
    appendU8(out, static_cast<uint8_t>('V'));
    appendU8(out, kVersion);
    appendU8(out, static_cast<uint8_t>(v.type));
    appendU8(out, 0);
    appendU8(out, 0);
    appendI64(out, expire_at_epoch_us);
    appendU32(out, static_cast<uint32_t>(payload.size()));
    appendBytes(out, payload.data(), payload.size());
    return out;
}

bool DataValueCodec::decode(const uint8_t* data, size_t len, DataValue* out, int64_t* expire_at_epoch_us) {
    if (!data || !out || !expire_at_epoch_us) return false;
    size_t off = 0;
    uint8_t m1 = 0, m2 = 0;
    if (!readU8(data, len, &off, &m1) || !readU8(data, len, &off, &m2)) return false;
    if (m1 != static_cast<uint8_t>('D') || m2 != static_cast<uint8_t>('V')) return false;

    uint8_t ver = 0;
    if (!readU8(data, len, &off, &ver)) return false;
    if (ver != kVersion) return false;

    uint8_t type_u8 = 0;
    if (!readU8(data, len, &off, &type_u8)) return false;
    uint8_t flags = 0, reserved = 0;
    if (!readU8(data, len, &off, &flags)) return false;
    if (!readU8(data, len, &off, &reserved)) return false;
    (void)flags;
    (void)reserved;

    int64_t exp = -1;
    if (!readI64(data, len, &off, &exp)) return false;
    *expire_at_epoch_us = exp;

    uint32_t payload_len = 0;
    if (!readU32(data, len, &off, &payload_len)) return false;
    if (off + payload_len > len) return false;

    const uint8_t* p = data + off;
    size_t plen = payload_len;
    size_t poff = 0;

    DataType t = static_cast<DataType>(type_u8);
    DataValue v;
    v.type = t;
    switch (t) {
        case DataType::STRING: {
            std::string s;
            if (!readString(p, plen, &poff, &s)) return false;
            v.string_value = std::move(s);
            break;
        }
        case DataType::LIST: {
            uint32_t n = 0;
            if (!readU32(p, plen, &poff, &n)) return false;
            for (uint32_t i = 0; i < n; ++i) {
                std::string item;
                if (!readString(p, plen, &poff, &item)) return false;
                v.list_value.push_back(std::move(item));
            }
            break;
        }
        case DataType::SET: {
            uint32_t n = 0;
            if (!readU32(p, plen, &poff, &n)) return false;
            for (uint32_t i = 0; i < n; ++i) {
                std::string item;
                if (!readString(p, plen, &poff, &item)) return false;
                v.set_value.insert(std::move(item));
            }
            break;
        }
        case DataType::HASH: {
            uint32_t n = 0;
            if (!readU32(p, plen, &poff, &n)) return false;
            for (uint32_t i = 0; i < n; ++i) {
                std::string k, val;
                if (!readString(p, plen, &poff, &k)) return false;
                if (!readString(p, plen, &poff, &val)) return false;
                v.hash_value.emplace(std::move(k), std::move(val));
            }
            break;
        }
        default:
            return false;
    }

    // storage2 内存记录使用绝对过期时间，不做 ttl_seconds 映射。
    v.ttl_seconds = -1;
    *out = std::move(v);
    return true;
}

} // namespace sunkv::storage2

