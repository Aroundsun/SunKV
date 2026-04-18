#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"
#include "storage2/persistence/PersistenceOrchestrator.h"
#include "storage2/persistence/SnapshotWriter.h"
#include "storage2/persistence/WalWriter.h"

namespace fs = std::filesystem;

static std::string tmpfile(const std::string& name) {
    auto base = fs::temp_directory_path();
    auto p = base / ("sunkv_" + name + "_" + std::to_string(static_cast<uint64_t>(::getpid())));
    return p.string();
}

static sunkv::storage2::Mutation makePut(const std::string& key, const std::string& val) {
    sunkv::storage2::Mutation m;
    m.type = sunkv::storage2::MutationType::PutRecord;
    m.key = key;
    sunkv::storage2::Record r;
    r.value = DataValue(val);
    r.expire_at_us = -1;
    r.version = 1;
    m.record = r;
    m.ts_us = 1;
    return m;
}

static sunkv::storage2::Mutation makeDel(const std::string& key) {
    sunkv::storage2::Mutation m;
    m.type = sunkv::storage2::MutationType::DelKey;
    m.key = key;
    m.ts_us = 2;
    return m;
}

int main() {
    const std::string snap_path = tmpfile("storage2_recover_precedence.snap");
    const std::string wal_path = tmpfile("storage2_recover_precedence.wal");
    (void)fs::remove(snap_path);
    (void)fs::remove(wal_path);

    // 准备 snapshot：k1=v_snap, k2=v2
    {
        std::vector<std::pair<std::string, sunkv::storage2::Record>> records;
        sunkv::storage2::Record r1;
        r1.value = DataValue("v_snap");
        r1.expire_at_us = -1;
        r1.version = 1;
        records.push_back({"k1", r1});

        sunkv::storage2::Record r2;
        r2.value = DataValue("v2");
        r2.expire_at_us = -1;
        r2.version = 1;
        records.push_back({"k2", r2});

        assert(sunkv::storage2::SnapshotWriter::writeToFile(records, snap_path));
    }

    // 准备 WAL：覆盖 k1、删除 k2、新增 k3
    {
        sunkv::storage2::WalWriter ww(wal_path);
        sunkv::storage2::MutationBatch b1;
        b1.push_back(makePut("k1", "v_wal"));
        b1.push_back(makeDel("k2"));
        b1.push_back(makePut("k3", "v3"));
        assert(ww.append(b1));
        ww.flush();
    }

    sunkv::storage2::PersistenceOrchestrator::Options opt;
    opt.enable_wal = true;
    opt.snapshot_path = snap_path;
    opt.wal_path = wal_path;

    sunkv::storage2::PersistenceOrchestrator orch(opt);

    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    // 第一次恢复
    assert(orch.recoverInto(engine));
    {
        auto g1 = engine.get("k1");
        assert(g1.status == sunkv::storage2::StatusCode::Ok);
        assert(g1.value.has_value() && *g1.value == "v_wal");

        auto g2 = engine.get("k2");
        assert(g2.status == sunkv::storage2::StatusCode::Ok);
        assert(!g2.value.has_value());

        auto g3 = engine.get("k3");
        assert(g3.status == sunkv::storage2::StatusCode::Ok);
        assert(g3.value.has_value() && *g3.value == "v3");
    }

    // 第二次恢复（幂等）：状态不应漂移
    assert(orch.recoverInto(engine));
    {
        auto g1 = engine.get("k1");
        assert(g1.status == sunkv::storage2::StatusCode::Ok);
        assert(g1.value.has_value() && *g1.value == "v_wal");

        auto g2 = engine.get("k2");
        assert(g2.status == sunkv::storage2::StatusCode::Ok);
        assert(!g2.value.has_value());

        auto g3 = engine.get("k3");
        assert(g3.status == sunkv::storage2::StatusCode::Ok);
        assert(g3.value.has_value() && *g3.value == "v3");
    }

    (void)fs::remove(snap_path);
    (void)fs::remove(wal_path);
    std::cout << "storage2_recover_precedence_idempotence_test passed." << std::endl;
    return 0;
}
