#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>

#include "storage2/persistence/WalCodec.h"
#include "storage2/persistence/WalReader.h"
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

static sunkv::storage2::Mutation makePut(int64_t ts, const std::string& k, const std::string& v) {
    sunkv::storage2::Mutation m;
    m.type = sunkv::storage2::MutationType::PutRecord;
    m.key = k;
    sunkv::storage2::Record r;
    r.value = DataValue(v);
    r.expire_at_us = -1;
    r.version = 1;
    m.record = r;
    m.ts_us = ts;
    return m;
}

int main() {
    const std::string wal_path = tmpfile("storage2_wal_trunc.bin");
    (void)fs::remove(wal_path);

    // 写入两条 mutation
    std::vector<sunkv::storage2::Mutation> muts;
    muts.push_back(makePut(1, "k1", "v1"));
    muts.push_back(makePut(2, "k2", "v2"));

    {
        sunkv::storage2::WalWriter ww(wal_path);
        sunkv::storage2::MutationBatch b;
        b.push_back(muts[0]);
        b.push_back(muts[1]);
        auto bytes = sunkv::storage2::WalCodec::encodeBatch(b);
        CHECK(ww.appendBytes(bytes.data(), bytes.size()));
        ww.flush();
    }

    // 人为截断最后 N 字节，模拟崩溃尾巴
    const auto sz = fs::file_size(wal_path);
    CHECK(sz > 8);
    fs::resize_file(wal_path, sz - 7);

    // 读取应成功，且至少回放出第一条（第二条可能缺失）
    sunkv::storage2::WalReader wr(wal_path);
    std::vector<sunkv::storage2::Mutation> got;
    CHECK(wr.readAllMutations(&got));
    CHECK(got.size() >= 1);
    CHECK(got[0].key == "k1");

    (void)fs::remove(wal_path);
    std::cout << "storage2_wal_truncation_tolerant_test passed." << std::endl;
    return 0;
}

