#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"
#include "storage2/persistence/SnapshotReader.h"
#include "storage2/persistence/SnapshotWriter.h"
#include "storage2/persistence/WalReader.h"
#include "storage2/persistence/WalWriter.h"

namespace fs = std::filesystem;

static std::string tmpfile(const std::string& name) {
    auto base = fs::temp_directory_path();
    auto p = base / ("sunkv_" + name + "_" + std::to_string(static_cast<uint64_t>(::getpid())));
    return p.string();
}

int main() {
    using sunkv::storage2::SnapshotReader;
    using sunkv::storage2::SnapshotWriter;

    const std::string wal_path = tmpfile("storage2_wal.bin");
    const std::string snap_path = tmpfile("storage2_snap.bin");
    (void)fs::remove(wal_path);
    (void)fs::remove(snap_path);

    // 1) 写入：engine -> mutations -> wal
    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    sunkv::storage2::WalWriter ww(wal_path);
    {
        auto r1 = engine.set("k1", "v1");
        assert(r1.status == sunkv::storage2::StatusCode::Ok);
        assert(ww.append(r1.mutations));

        auto r2 = engine.lpush("l1", {"a", "b"});
        assert(r2.status == sunkv::storage2::StatusCode::Ok);
        assert(ww.append(r2.mutations));

        auto r3 = engine.hset("h1", "f1", "x");
        assert(r3.status == sunkv::storage2::StatusCode::Ok);
        assert(ww.append(r3.mutations));
        ww.flush();
    }

    // 2) 快照：从 backend 全量写出
    {
        auto backend2 = std::make_unique<sunkv::storage2::InMemoryBackend>();
        sunkv::storage2::StorageEngine engine2(std::move(backend2));
        // 先把状态“复制”一份：直接用 mutations 回放到 backend（简化：重新执行命令）。
        (void)engine2.set("k1", "v1");
        (void)engine2.lpush("l1", {"a", "b"});
        (void)engine2.hset("h1", "f1", "x");

        // 拿出 engine2 内部 backend 不现实（封装），所以这里直接用 SnapshotWriter 针对独立 backend 走一遍：
        // v0.1：我们用一个新的 backend 手工 putRecord 构造快照验证编码/解码链路。
        auto b = std::make_unique<sunkv::storage2::InMemoryBackend>();
        sunkv::storage2::Record rr;
        rr.value = DataValue("v1");
        rr.expire_at_us = -1;
        rr.version = 1;
        b->putRecord("k1", rr);
        assert(SnapshotWriter::writeToFile(*b, snap_path));
        // 原子写回归：不会遗留 .tmp
        assert(!fs::exists(snap_path + ".tmp"));

        // 再写一版不同内容，确认可覆盖且可读
        rr.value = DataValue("v2");
        b->putRecord("k1", rr);
        assert(SnapshotWriter::writeToFile(*b, snap_path));
        assert(!fs::exists(snap_path + ".tmp"));
    }

    // 3) 读快照 -> 读 WAL -> 应用到 backend
    {
        sunkv::storage2::InMemoryBackend b;
        assert(SnapshotReader::loadFromFile(b, snap_path));

        // 读 WAL
        sunkv::storage2::WalReader wr(wal_path);
        std::vector<sunkv::storage2::MutationBatch> batches;
        assert(wr.readAll(&batches));
        assert(!batches.empty());

        // 应用：v0.1 简化（只测 PutRecord/DelKey/ClearAll 的后端效果）
        for (const auto& batch : batches) {
            for (const auto& m : batch) {
                if (m.type == sunkv::storage2::MutationType::PutRecord) {
                    assert(m.record.has_value());
                    b.putRecord(m.key, *m.record);
                } else if (m.type == sunkv::storage2::MutationType::DelKey) {
                    (void)b.delKey(m.key);
                } else if (m.type == sunkv::storage2::MutationType::ClearAll) {
                    b.clearAll();
                }
            }
        }

        // 最终验证：k1 至少存在（来自 WAL 或快照），类型为 STRING 值 v1
        auto r = b.getRecord("k1");
        assert(r.has_value());
        assert(r->value.type == DataType::STRING);
        // 最终会被 WAL 的 set(k1,v1) 覆盖
        assert(r->value.string_value.str() == "v1");
    }

    (void)fs::remove(wal_path);
    (void)fs::remove(snap_path);

    std::cout << "storage2_persistence_minicircle_test passed." << std::endl;
    return 0;
}

