#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <cstdlib>

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

static sunkv::storage2::Mutation makeDel(int64_t ts, const std::string& k) {
    sunkv::storage2::Mutation m;
    m.type = sunkv::storage2::MutationType::DelKey;
    m.key = k;
    m.ts_us = ts;
    return m;
}

static void assertEq(const sunkv::storage2::Mutation& a, const sunkv::storage2::Mutation& b) {
    if (a.type != b.type) std::abort();
    if (a.key != b.key) std::abort();
    if (a.ts_us != b.ts_us) std::abort();
    if (a.type == sunkv::storage2::MutationType::PutRecord) {
        if (!a.record.has_value() || !b.record.has_value()) std::abort();
        if (a.record->expire_at_us != b.record->expire_at_us) std::abort();
        if (a.record->version != b.record->version) std::abort();
        if (a.record->value.type != b.record->value.type) std::abort();
        if (a.record->value.string_value != b.record->value.string_value) std::abort();
    }
}

int main() {
    const std::string wal_path = tmpfile("storage2_wal_roundtrip.bin");
    (void)fs::remove(wal_path);

    // 生成一串 mutations
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> op_dist(0, 1); // 0=put,1=del
    std::uniform_int_distribution<int> key_dist(0, 9);

    std::vector<sunkv::storage2::Mutation> expected;
    expected.reserve(2000);
    int64_t ts = 1000;
    for (int i = 0; i < 2000; ++i) {
        std::string key = "k" + std::to_string(key_dist(rng));
        if (op_dist(rng) == 0) {
            expected.push_back(makePut(ts, key, "v" + std::to_string(i)));
        } else {
            expected.push_back(makeDel(ts, key));
        }
        ++ts;
    }
    // 编码写入：分批写，覆盖 writer 的多次 appendBytes 行为
    {
        sunkv::storage2::WalWriter ww(wal_path);
        for (size_t i = 0; i < expected.size(); i += 128) {
            sunkv::storage2::MutationBatch batch;
            const size_t end = std::min(expected.size(), i + 128);
            for (size_t j = i; j < end; ++j) batch.push_back(expected[j]);
            auto bytes = sunkv::storage2::WalCodec::encodeBatch(batch);
            CHECK(ww.appendBytes(bytes.data(), bytes.size()));
        }
        ww.flush();
    }

    // 读回并比对
    sunkv::storage2::WalReader wr(wal_path);
    std::vector<sunkv::storage2::Mutation> got;
    CHECK(wr.readAllMutations(&got));
    CHECK(got.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        assertEq(got[i], expected[i]);
    }

    (void)fs::remove(wal_path);
    std::cout << "storage2_wal_roundtrip_test passed." << std::endl;
    return 0;
}

