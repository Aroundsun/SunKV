#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "storage2/persistence/WalWriter.h"

namespace fs = std::filesystem;

#define CHECK(cond)                 \
    do {                            \
        if (!(cond)) std::abort();  \
    } while (0)

static std::string tmpfile(const std::string& name) {
    auto base = fs::temp_directory_path();
    auto p = base / ("sunkv_" + name + "_" + std::to_string(static_cast<uint64_t>(::getpid())));
    return p.string();
}

static std::uintmax_t chain_bytes(const std::string& primary) {
    std::uintmax_t total = 0;
    std::error_code ec;
    if (fs::exists(primary, ec)) {
        total += fs::file_size(primary, ec);
    }
    const fs::path dir = fs::path(primary).parent_path();
    const std::string base = fs::path(primary).filename().string();
    if (!fs::exists(dir, ec)) {
        return total;
    }
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string fn = ent.path().filename().string();
        if (fn.size() <= base.size() + 1) continue;
        if (fn.compare(0, base.size(), base) != 0) continue;
        if (fn[base.size()] != '.') continue;
        total += fs::file_size(ent.path(), ec);
    }
    return total;
}

int main() {
    const std::string wal_path = tmpfile("storage2_wal_max_enforced.bin");
    (void)fs::remove(wal_path);
    for (int i = 1; i <= 20; ++i) {
        (void)fs::remove(wal_path + "." + std::to_string(i));
    }

    constexpr size_t kMax = 64 * 1024;
    constexpr size_t kPayload = 200 * 1024;
    std::vector<uint8_t> payload(kPayload, static_cast<uint8_t>('z'));

    {
        sunkv::storage2::WalWriter ww(wal_path, kMax);
        CHECK(ww.appendBytes(payload.data(), payload.size()));
        ww.flush();
    }

    CHECK(fs::exists(wal_path));
    CHECK(fs::file_size(wal_path) <= kMax);
    CHECK(fs::exists(wal_path + ".1"));
    CHECK(chain_bytes(wal_path) == kPayload);

    (void)fs::remove(wal_path);
    for (int i = 1; i <= 20; ++i) {
        (void)fs::remove(wal_path + "." + std::to_string(i));
    }

    std::cout << "storage2_wal_max_file_enforced_test passed." << std::endl;
    return 0;
}
