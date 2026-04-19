#pragma once

#include <cstdint>
#include <string>

#include "../api/StorageResult.h"

namespace sunkv::storage2 {

class WalWriter final {
public:
    static constexpr uint8_t kVersion = 1;

    /// max_file_bytes==0 表示不限制、不滚动 WAL 文件
    explicit WalWriter(std::string path, size_t max_file_bytes = 0);
    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    bool append(const MutationBatch& batch);
    bool appendBytes(const uint8_t* data, size_t len);
    void flush();

private:
    bool rotate_();

    std::string path_;
    size_t max_file_bytes_{0};
    int fd_{-1};
};

} // namespace sunkv::storage2
