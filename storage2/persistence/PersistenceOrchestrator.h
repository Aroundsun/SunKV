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

// v0.1：持久化编排器骨架
//
// 目标：
// - 接收 MutationBatch（同步或异步）
// - 将来在这里统一接入：WAL 写入、刷盘策略、快照触发、恢复流程
//
// 当前阶段仅提供“接收与回调”能力，便于后续逐步接入真实 IO。
class PersistenceOrchestrator final {
public:
    struct Options {
        bool async{true};
        // 异步队列上限：达到上限时 submit 会阻塞等待（背压），不再静默丢弃。
        // 传 0 会被视为 1，避免“永远满队列”。
        size_t max_queue{100000};

        // 可选：启用 WAL 落盘（v0.1 先支持 WAL；快照后续再接入调度）
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

        // 可选：恢复时使用的快照路径（若不存在/为空则跳过）
        std::string snapshot_path{};
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

    // 统计：历史丢弃计数（当前实现默认背压阻塞，不应增长；保留用于兼容与观测）。
    uint64_t droppedBatches() const { return dropped_batches_.load(); }

private:
    void workerLoop_();

    Options opt_{};
    std::vector<MutationConsumer> consumers_;
    std::unique_ptr<WalWriter> wal_writer_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<MutationBatch> queue_;

    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> dropped_batches_{0};
    std::optional<std::thread> worker_;
};

} // namespace sunkv::storage2

