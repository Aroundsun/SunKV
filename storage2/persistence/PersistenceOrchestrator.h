#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <memory>
#include <vector>

#include "../api/StorageResult.h"

namespace sunkv::storage2 {

class WalWriter;
class StorageEngine;

// 持久化编排：WAL 异步队列、刷盘策略、周期性/手动快照、恢复（快照 + WAL 链）。
class PersistenceOrchestrator final {
public:
    // 选项
    struct Options {
        bool async{true};
        // 异步队列上限：达到上限时 submit 会阻塞等待（背压），不再静默丢弃。
        // 传 0 会被视为 1，避免“永远满队列”。
        size_t max_queue{100000};

        bool enable_wal{false};
        std::string wal_path{};

        enum class WalFlushPolicy {
            Never = 0,    // 不主动 fsync（依赖 OS）
            Always = 1,   // 每次 submit 批次都 fsync
            Periodic = 2, // 定期 fsync
        };
        WalFlushPolicy wal_flush_policy{WalFlushPolicy::Periodic};
        int64_t wal_flush_interval_ms{1000}; // Periodic 生效

        // group commit（仅 async 生效）：
        // - linger：拿到第一批后最多再等一小段时间合并更多提交
        // - max_mutations/max_bytes：达到上限就切分成多次 WAL append（每次 append 一次 write）
        int64_t wal_group_commit_linger_ms{2};
        size_t wal_group_commit_max_mutations{8192};
        size_t wal_group_commit_max_bytes{2 * 1024 * 1024};

        /// WAL 单文件上限（MB），0 表示不滚动
        int max_wal_file_size_mb{0};

        /// 快照文件路径（恢复与写入共用；为空则不在 recover/takeSnapshot 中使用）
        std::string snapshot_path{};

        /// 非拥有指针：用于 dump 写快照；生命周期须长于本 Orchestrator（见 Storage2Components 析构顺序）
        StorageEngine* engine_for_snapshot{nullptr};

        /// 是否启用周期性快照线程（仍要求 snapshot_path 非空且 engine 非空且 interval>0）
        bool enable_snapshot{false};
        /// 周期秒数；<=0 表示不启动周期线程
        int snapshot_interval_seconds{0};
    };

    using MutationConsumer = std::function<void(const MutationBatch&)>;

    PersistenceOrchestrator();
    explicit PersistenceOrchestrator(Options opt);
    ~PersistenceOrchestrator();

    PersistenceOrchestrator(const PersistenceOrchestrator&) = delete;
    PersistenceOrchestrator& operator=(const PersistenceOrchestrator&) = delete;

    // 注册消费回调（可注册多个）。回调必须线程安全。
    void addConsumer(MutationConsumer consumer);

    // 接收一次写事件批（来自 StorageEngine 的返回值）。
    void submit(MutationBatch batch);

    // 同步刷盘（只在启用 WAL 时生效）。
    void flush();

    // 最小恢复入口：Snapshot -> WAL 回放到 engine（由 engine 落到 backend）。
    // 约定：
    // - snapshot_path 为空则不加载快照
    // - wal_path 为空/不存在则视为无 WAL
    bool recoverInto(StorageEngine& engine);

    /// 将当前 engine 全量 dump 写入 snapshot_path（与 recoverInto 使用同一路径）。
    /// 尽力而为一致性，不打停写入。
    bool takeSnapshotNow();

    /// 停止周期性快照线程并 join（Server 关闭时在 gracefulShutdown 之前调用，避免与最终快照并发）。
    void stopPeriodicSnapshot();

    // 统计：历史丢弃计数（当前实现默认背压阻塞，不应增长；保留用于兼容与观测）。
    uint64_t droppedBatches() const { return dropped_batches_.load(); }

private:
    void workerLoop_();
    void snapshotIntervalLoop_();

    Options opt_{};
    std::vector<MutationConsumer> consumers_;
    std::unique_ptr<WalWriter> wal_writer_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<MutationBatch> queue_;

    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> dropped_batches_{0};
    std::optional<std::thread> worker_;

    std::atomic<bool> snapshot_interval_running_{false};
    std::thread snapshot_thread_;
};

} // namespace sunkv::storage2
