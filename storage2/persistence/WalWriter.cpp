#include "WalWriter.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
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
} // namespace

WalWriter::WalWriter(std::string path) : path_(std::move(path)) {
    fd_ = ::open(path_.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
}

WalWriter::~WalWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
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
    return writeAll(fd_, data, len);
}

void WalWriter::flush() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

} // namespace sunkv::storage2

