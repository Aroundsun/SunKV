#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <unistd.h>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"
#include "storage2/persistence/PersistenceOrchestrator.h"

namespace fs = std::filesystem;

static std::string tmpfile(const std::string& name) {
    auto base = fs::temp_directory_path();
    auto p = base / ("sunkv_" + name + "_" + std::to_string(static_cast<uint64_t>(::getpid())));
    return p.string();
}

int main() {
    const std::string wal_path = tmpfile("storage2_orch_wal.bin");
    (void)fs::remove(wal_path);

    sunkv::storage2::PersistenceOrchestrator::Options opt;
    opt.async = true;
    opt.max_queue = 1; // 强制触发背压路径
    opt.enable_wal = true;
    opt.wal_path = wal_path;
    opt.wal_flush_policy = sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Always;

    sunkv::storage2::PersistenceOrchestrator orch(opt);

    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    // 连续提交多条，验证异步满队列时不会静默丢弃。
    for (int i = 0; i < 200; ++i) {
        const std::string key = "k" + std::to_string(i);
        const std::string val = "v" + std::to_string(i);
        auto r = engine.set(key, val);
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        orch.submit(std::move(r.mutations));
    }
    orch.flush();
    assert(orch.droppedBatches() == 0);

    // 恢复到一个全新 backend
    auto recovered_backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine recovered_engine(std::move(recovered_backend));
    assert(orch.recoverInto(recovered_engine));

    for (int i = 0; i < 200; ++i) {
        const std::string key = "k" + std::to_string(i);
        const std::string val = "v" + std::to_string(i);
        auto rec = recovered_engine.get(key);
        assert(rec.status == sunkv::storage2::StatusCode::Ok);
        assert(rec.value.has_value());
        assert(*rec.value == val);
    }

    (void)fs::remove(wal_path);
    std::cout << "storage2_orchestrator_recover_test passed." << std::endl;
    return 0;
}

