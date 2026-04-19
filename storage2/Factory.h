#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "api/IStorageAPI.h"
#include "engine/StorageEngine.h"
#include "persistence/PersistenceOrchestrator.h"

namespace sunkv::storage2 {

struct Storage2WiringOptions {
    /// InMemoryBackend 配额（字节），0 表示不限制
    size_t max_storage_bytes{0};

    // persistence
    bool enable_wal{false};
    std::string wal_path{};
    std::string snapshot_path{};
    PersistenceOrchestrator::Options::WalFlushPolicy wal_flush_policy{
        PersistenceOrchestrator::Options::WalFlushPolicy::Periodic};
    int64_t wal_flush_interval_ms{1000};

    bool wal_async{true};
    size_t wal_max_queue{100000};
    int64_t wal_group_commit_linger_ms{2};
    size_t wal_group_commit_max_mutations{8192};
    size_t wal_group_commit_max_bytes{2 * 1024 * 1024};
    int max_wal_file_size_mb{0};

    /// 与 Config::enable_snapshot 一致；周期性快照由 PersistenceOrchestrator 内线程执行
    bool enable_snapshot{false};
    /// 与 Config::snapshot_interval_seconds 对齐；<=0 不启动周期线程
    int snapshot_interval_seconds{0};
};

// 成员析构顺序：orchestrator -> engine 指针（无析构）-> api。
// orchestrator 可能持有 engine 非拥有指针，须先于 api（及其中 StorageEngine）析构。
struct Storage2Components {
    std::unique_ptr<IStorageAPI> api;                       // 给 Server 用
    StorageEngine* engine{nullptr};                          // 给 recover 用（对象所有权在 api 链内）
    std::unique_ptr<PersistenceOrchestrator> orchestrator;   // 可选
};

// 一键组装 storage2：backend + engine +（可选）orchestrator
Storage2Components createStorage2(Storage2WiringOptions opt);

} // namespace sunkv::storage2
