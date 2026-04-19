#include "Factory.h"

#include <utility>

#include "backend/InMemoryBackend.h"
#include "decorators/OrchestratedStorageAPI.h"

namespace sunkv::storage2 {

Storage2Components createStorage2(Storage2WiringOptions opt) {
    // 1) backend
    std::unique_ptr<IBackend> backend = std::make_unique<InMemoryBackend>(opt.max_storage_bytes);

    // 2) engine
    auto engine = std::make_unique<StorageEngine>(std::move(backend));
    StorageEngine* engine_ptr = engine.get();

    // 3) api
    std::unique_ptr<IStorageAPI> api = std::move(engine);

    // 4) orchestrator（可选）
    std::unique_ptr<PersistenceOrchestrator> orch;
    if (opt.enable_wal || !opt.snapshot_path.empty()) {
        PersistenceOrchestrator::Options popt;
        popt.async = opt.wal_async;
        popt.max_queue = opt.wal_max_queue;
        popt.enable_wal = opt.enable_wal;
        popt.wal_path = opt.wal_path;
        popt.snapshot_path = opt.snapshot_path;
        popt.wal_flush_policy = opt.wal_flush_policy;
        popt.wal_flush_interval_ms = opt.wal_flush_interval_ms;
        popt.wal_group_commit_linger_ms = opt.wal_group_commit_linger_ms;
        popt.wal_group_commit_max_mutations = opt.wal_group_commit_max_mutations;
        popt.wal_group_commit_max_bytes = opt.wal_group_commit_max_bytes;
        popt.max_wal_file_size_mb = opt.max_wal_file_size_mb;
        popt.engine_for_snapshot = engine_ptr;
        popt.enable_snapshot = opt.enable_snapshot;
        popt.snapshot_interval_seconds = opt.snapshot_interval_seconds;
        orch = std::make_unique<PersistenceOrchestrator>(popt);
    }

    // 5) 最外层：自动 submit mutations（避免 Server 手动提交）
    if (orch) {
        api = std::make_unique<OrchestratedStorageAPI>(std::move(api), orch.get());
    }

    Storage2Components c;
    c.api = std::move(api);
    c.engine = engine_ptr;
    c.orchestrator = std::move(orch);
    return c;
}

} // namespace sunkv::storage2
