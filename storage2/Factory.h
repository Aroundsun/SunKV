#pragma once

#include <memory>
#include <string>

#include "api/IStorageAPI.h"
#include "engine/StorageEngine.h"
#include "persistence/PersistenceOrchestrator.h"

namespace sunkv::storage2 {

struct Storage2WiringOptions {
    // persistence
    bool enable_wal{false};
    std::string wal_path{};
    std::string snapshot_path{};
    PersistenceOrchestrator::Options::WalFlushPolicy wal_flush_policy{
        PersistenceOrchestrator::Options::WalFlushPolicy::Periodic};
    int64_t wal_flush_interval_ms{1000};
};

struct Storage2Components {
    std::unique_ptr<IStorageAPI> api;                       // 给 Server 用
    StorageEngine* engine{nullptr};                          // 给 recover 用（对象所有权在 api 链内）
    std::unique_ptr<PersistenceOrchestrator> orchestrator;   // 可选
};

// 一键组装 storage2：backend + engine +（可选）cache/metrics +（可选）orchestrator
Storage2Components createStorage2(Storage2WiringOptions opt);

} // namespace sunkv::storage2

