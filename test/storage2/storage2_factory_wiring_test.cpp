#include <filesystem>
#include <iostream>
#include <string>

#include <unistd.h>

#include "storage2/Factory.h"

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

int main() {
    const std::string wal_path = tmpfile("storage2_factory_wal.bin");
    (void)fs::remove(wal_path);

    sunkv::storage2::Storage2WiringOptions opt;
    opt.enable_wal = true;
    opt.wal_path = wal_path;
    opt.wal_flush_policy = sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Always;

    auto c = sunkv::storage2::createStorage2(opt);
    CHECK(c.api != nullptr);
    CHECK(c.engine != nullptr);
    CHECK(c.orchestrator != nullptr);

    // 写入一条数据并落 WAL（不应需要手动 submit）
    auto r = c.api->set("k1", "v1");
    CHECK(r.status == sunkv::storage2::StatusCode::Ok);
    c.orchestrator->flush();

    // 恢复到新组件（只开 WAL，不开 cache/metrics 也行）
    sunkv::storage2::Storage2WiringOptions opt2;
    opt2.enable_wal = true;
    opt2.wal_path = wal_path;
    auto c2 = sunkv::storage2::createStorage2(opt2);
    CHECK(c2.orchestrator->recoverInto(*c2.engine));

    auto g = c2.api->get("k1");
    CHECK(g.status == sunkv::storage2::StatusCode::Ok);
    CHECK(g.value.has_value());
    CHECK(*g.value == "v1");

    (void)fs::remove(wal_path);

    // takeSnapshotNow：engine 指针由 Factory 注入，手动快照写入 snapshot_path（不启周期线程以免阻塞用例）
    const std::string snap_path = tmpfile("storage2_factory_snap.bin");
    (void)fs::remove(snap_path);
    {
        sunkv::storage2::Storage2WiringOptions opt3;
        opt3.enable_wal = true;
        opt3.wal_path = tmpfile("storage2_factory_wal2.bin");
        opt3.snapshot_path = snap_path;
        (void)fs::remove(opt3.wal_path);
        auto c3 = sunkv::storage2::createStorage2(opt3);
        CHECK(c3.orchestrator != nullptr);
        CHECK(c3.engine != nullptr);
        auto r0 = c3.api->set("sk", "sv");
        CHECK(r0.status == sunkv::storage2::StatusCode::Ok);
        c3.orchestrator->flush();
        CHECK(c3.orchestrator->takeSnapshotNow());
        CHECK(fs::exists(snap_path));
        (void)fs::remove(opt3.wal_path);
        (void)fs::remove(snap_path);
    }

    std::cout << "storage2_factory_wiring_test passed." << std::endl;
    return 0;
}

