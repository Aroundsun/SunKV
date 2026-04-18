#include "WalCodec.h"

#include <cstdint>
#include <string>
#include <vector>

#include "RecordCodec.h"

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
static void appendBytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); }
static void appendString(std::vector<uint8_t>& out, const std::string& s) {
    appendU32(out, static_cast<uint32_t>(s.size()));
    appendBytes(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
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
static bool readString(const uint8_t* data, size_t len, size_t* off, std::string* out) {
    uint32_t n = 0;
    if (!readU32(data, len, off, &n)) return false;
    if (*off + n > len) return false;
    out->assign(reinterpret_cast<const char*>(data + *off), n);
    *off += n;
    return true;
}
} // namespace

void WalCodec::appendMutation(std::vector<uint8_t>& out, const Mutation& m) {
    // entry 格式：
    // magic(2)='W''2'
    // version(1)
    // ts_us(i64)
    // type(u8)
    // key(str)
    // record_bytes_len(u32)
    // record_bytes(optional: PutRecord)
    appendU8(out, static_cast<uint8_t>('W'));
    appendU8(out, static_cast<uint8_t>('2'));
    appendU8(out, kVersion);
    appendI64(out, m.ts_us);
    appendU8(out, static_cast<uint8_t>(m.type));
    appendString(out, m.key);

    if (m.type == MutationType::PutRecord) {
        if (!m.record.has_value()) {
            appendU32(out, 0);
            return;
        }
        auto rb = RecordCodec::encode(*m.record);
        appendU32(out, static_cast<uint32_t>(rb.size()));
        appendBytes(out, rb.data(), rb.size());
    } else {
        appendU32(out, 0);
    }
}

std::vector<uint8_t> WalCodec::encodeBatch(const MutationBatch& batch) {
    std::vector<uint8_t> out;
    out.reserve(batch.size() * 128);
    for (const auto& m : batch) {
        appendMutation(out, m);
    }
    return out;
}

bool WalCodec::decodeOne(const uint8_t* data, size_t len, size_t* off, Mutation* out) {
    return decodeOneStatus(data, len, off, out) == DecodeStatus::Ok;
}

WalCodec::DecodeStatus WalCodec::decodeOneStatus(const uint8_t* data, size_t len, size_t* off, Mutation* out) {
    if (!data || !off || !out) return DecodeStatus::Corrupt;
    uint8_t m0 = 0, m1 = 0, ver = 0;
    const size_t start = *off;
    if (!readU8(data, len, off, &m0) || !readU8(data, len, off, &m1) || !readU8(data, len, off, &ver)) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }
    if (m0 != static_cast<uint8_t>('W') || m1 != static_cast<uint8_t>('2')) return DecodeStatus::Corrupt;
    if (ver != kVersion) return DecodeStatus::Corrupt;

    Mutation mu;
    if (!readI64(data, len, off, &mu.ts_us)) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }
    uint8_t t = 0;
    if (!readU8(data, len, off, &t)) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }
    mu.type = static_cast<MutationType>(t);
    if (!readString(data, len, off, &mu.key)) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }

    uint32_t rb_len = 0;
    if (!readU32(data, len, off, &rb_len)) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }
    if (*off + rb_len > len) {
        *off = start;
        return DecodeStatus::IncompleteTail;
    }
    if (rb_len > 0) {
        Record r;
        if (!RecordCodec::decode(data + *off, rb_len, &r)) return DecodeStatus::Corrupt;
        mu.record = std::move(r);
        *off += rb_len;
    }
    *out = std::move(mu);
    return DecodeStatus::Ok;
}

} // namespace sunkv::storage2

