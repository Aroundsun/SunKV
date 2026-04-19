#include "WalWriter.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "WalCodec.h"

namespace sunkv::storage2 {

namespace {
static bool writeAll(int fd, const uint8_t* p, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::write(fd, p + off, n - off);
        if (w <= 0) return false;
        off += static_cast<size_t>(w);
    }
    return true;
}

static bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static int maxNumericWalSuffix(const std::string& wal_path) {
    namespace fs = std::filesystem;
    fs::path p(wal_path);
    const fs::path dir = p.parent_path();
    const std::string base = p.filename().string();
    int max_n = 0;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return 0;
    }
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string fn = ent.path().filename().string();
        if (fn.size() <= base.size() + 1) continue;
        if (fn.compare(0, base.size(), base) != 0) continue;
        if (fn[base.size()] != '.') continue;
        const std::string suf = fn.substr(base.size() + 1);
        if (!allDigits(suf)) continue;
        try {
            const int n = std::stoi(suf);
            if (n > max_n) max_n = n;
        } catch (...) {
        }
    }
    return max_n;
}
} // namespace

WalWriter::WalWriter(std::string path, size_t max_file_bytes)
    : path_(std::move(path)), max_file_bytes_(max_file_bytes) {
    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
}

WalWriter::~WalWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool WalWriter::rotate_() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(path_, ec) && fs::file_size(path_, ec) > 0) {
        const int next = maxNumericWalSuffix(path_) + 1;
        const std::string dest = path_ + "." + std::to_string(next);
        if (::rename(path_.c_str(), dest.c_str()) != 0) {
            return false;
        }
    }
    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
    return fd_ >= 0;
}

bool WalWriter::append(const MutationBatch& batch) {
    if (fd_ < 0) return false;
    if (batch.empty()) return true;

    auto all = WalCodec::encodeBatch(batch);
    return appendBytes(all.data(), all.size());
}

bool WalWriter::appendBytes(const uint8_t* data, size_t len) {
    if (fd_ < 0) return false;
    if (!data || len == 0) return true;

    // max_file_bytes_==0：不限制单文件大小。
    if (max_file_bytes_ == 0) {
        return writeAll(fd_, data, len);
    }

    // 有上限时：单次 append 可能携带「合并后的大 buffer」（异步 group commit），
    // 不能依赖「单条 mutation < 上限」；必须在同一 appendBytes 内分块写并滚动，
    // 否则会出现 wal2.bin 远超 max_wal_file_size_mb 仍不 rename 的现象。
    size_t offset = 0;
    while (offset < len) {
        struct stat st {};
        if (::fstat(fd_, &st) != 0) {
            return false;
        }
        const auto cur = static_cast<size_t>(st.st_size);
        if (cur >= max_file_bytes_) {
            if (!rotate_()) return false;
            continue;
        }
        const size_t room = max_file_bytes_ - cur;
        if (room == 0) {
            if (!rotate_()) return false;
            continue;
        }
        const size_t chunk = std::min(len - offset, room);
        if (!writeAll(fd_, data + offset, chunk)) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

void WalWriter::flush() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

} // namespace sunkv::storage2
