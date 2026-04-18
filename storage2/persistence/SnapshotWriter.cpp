#include "SnapshotWriter.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

#include "RecordCodec.h"

namespace sunkv::storage2 {

namespace {
static void writeU8(std::ostream& out, uint8_t v) { out.put(static_cast<char>(v)); }
static void writeU32(std::ostream& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) writeU8(out, static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}
static void writeBytes(std::ostream& out, const uint8_t* p, size_t n) {
    out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
}
static void writeString(std::ostream& out, const std::string& s) {
    writeU32(out, static_cast<uint32_t>(s.size()));
    if (!s.empty()) writeBytes(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

static bool fsyncFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

static bool fsyncDir(const std::filesystem::path& dir) {
    if (dir.empty()) return true;
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}
} // namespace

bool SnapshotWriter::writeToFile(IBackend& backend, const std::string& path) {
    auto ks = backend.keys();
    std::vector<std::pair<std::string, Record>> records;
    records.reserve(ks.size());
    for (const auto& k : ks) {
        auto r = backend.getRecord(k);
        if (!r.has_value()) continue;
        records.push_back({k, *r});
    }

    return writeToFile(records, path);
}

bool SnapshotWriter::writeToFile(const std::vector<std::pair<std::string, Record>>& records, const std::string& path) {
    namespace fs = std::filesystem;
    const fs::path final_path(path);
    const fs::path parent = final_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) return false;
    }

    // 原子写：先写到同目录临时文件，落盘后 rename 覆盖。
    const fs::path tmp_path = final_path.string() + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    // 格式：
    // magic(2)='S''2'
    // version(1)
    // count(u32)
    // repeated:
    //   key(str)
    //   record_len(u32)
    //   record_bytes(RecordCodec)
    writeU8(out, static_cast<uint8_t>('S'));
    writeU8(out, static_cast<uint8_t>('2'));
    writeU8(out, kVersion);
    writeU32(out, static_cast<uint32_t>(records.size()));

    for (const auto& kv : records) {
        auto rb = RecordCodec::encode(kv.second);
        writeString(out, kv.first);
        writeU32(out, static_cast<uint32_t>(rb.size()));
        if (!rb.empty()) writeBytes(out, rb.data(), rb.size());
        if (!out.good()) {
            out.close();
            (void)fs::remove(tmp_path);
            return false;
        }
    }
    out.flush();
    if (!out.good()) {
        out.close();
        (void)fs::remove(tmp_path);
        return false;
    }
    out.close();

    if (!fsyncFile(tmp_path.string())) {
        (void)fs::remove(tmp_path);
        return false;
    }

    std::error_code ec;
    fs::rename(tmp_path, final_path, ec);
    if (ec) {
        // 部分平台/场景 rename 覆盖失败，尝试先删除再 rename（同目录仍可保持较强原子语义）。
        std::error_code ec_rm;
        fs::remove(final_path, ec_rm);
        ec.clear();
        fs::rename(tmp_path, final_path, ec);
        if (ec) {
            (void)fs::remove(tmp_path);
            return false;
        }
    }

    if (!fsyncDir(parent)) {
        return false;
    }
    return true;
}

} // namespace sunkv::storage2

