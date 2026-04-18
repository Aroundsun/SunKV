#include "SnapshotReader.h"

#include <cstdint>
#include <fstream>
#include <vector>

#include "RecordCodec.h"

namespace sunkv::storage2 {

namespace {
static bool readU8(std::istream& in, uint8_t* out) {
    char c;
    if (!in.get(reinterpret_cast<char&>(c))) return false;
    *out = static_cast<uint8_t>(c);
    return true;
}
static bool readU32(std::istream& in, uint32_t* out) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = 0;
        if (!readU8(in, &b)) return false;
        v |= (static_cast<uint32_t>(b) << (i * 8));
    }
    *out = v;
    return true;
}
static bool readString(std::istream& in, std::string* out) {
    uint32_t n = 0;
    if (!readU32(in, &n)) return false;
    out->assign(n, '\0');
    if (n == 0) return true;
    in.read(out->data(), static_cast<std::streamsize>(n));
    return in.good();
}
static bool readBytes(std::istream& in, uint32_t n, std::vector<uint8_t>* out) {
    out->assign(n, 0);
    if (n == 0) return true;
    in.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(n));
    return in.good();
}
} // namespace

bool SnapshotReader::loadFromFile(IBackend& backend, const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    uint8_t m0 = 0, m1 = 0, ver = 0;
    if (!readU8(in, &m0) || !readU8(in, &m1) || !readU8(in, &ver)) return false;
    if (m0 != static_cast<uint8_t>('S') || m1 != static_cast<uint8_t>('2')) return false;
    if (ver != kVersion) return false;

    uint32_t count = 0;
    if (!readU32(in, &count)) return false;

    backend.clearAll();
    for (uint32_t i = 0; i < count; ++i) {
        std::string key;
        if (!readString(in, &key)) return false;
        uint32_t rb_len = 0;
        if (!readU32(in, &rb_len)) return false;
        std::vector<uint8_t> rb;
        if (!readBytes(in, rb_len, &rb)) return false;
        Record r;
        if (!RecordCodec::decode(rb, &r)) return false;
        backend.putRecord(key, r);
    }
    return true;
}

bool SnapshotReader::readFromFile(const std::string& path, std::vector<std::pair<std::string, Record>>* out) {
    if (!out) return false;
    out->clear();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    uint8_t m0 = 0, m1 = 0, ver = 0;
    if (!readU8(in, &m0) || !readU8(in, &m1) || !readU8(in, &ver)) return false;
    if (m0 != static_cast<uint8_t>('S') || m1 != static_cast<uint8_t>('2')) return false;
    if (ver != kVersion) return false;

    uint32_t count = 0;
    if (!readU32(in, &count)) return false;
    out->reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        std::string key;
        if (!readString(in, &key)) return false;
        uint32_t rb_len = 0;
        if (!readU32(in, &rb_len)) return false;
        std::vector<uint8_t> rb;
        if (!readBytes(in, rb_len, &rb)) return false;
        Record r;
        if (!RecordCodec::decode(rb, &r)) return false;
        out->push_back({std::move(key), std::move(r)});
    }
    return true;
}

} // namespace sunkv::storage2

