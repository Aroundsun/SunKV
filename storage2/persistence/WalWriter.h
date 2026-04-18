#pragma once

#include <cstdint>
#include <string>

#include "../api/StorageResult.h"

namespace sunkv::storage2 {

class WalWriter final {
public:
    static constexpr uint8_t kVersion = 1;

    explicit WalWriter(std::string path);
    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;

    bool append(const MutationBatch& batch);
    bool appendBytes(const uint8_t* data, size_t len);
    void flush();

private:
    std::string path_;
    int fd_{-1};
};

} // namespace sunkv::storage2

