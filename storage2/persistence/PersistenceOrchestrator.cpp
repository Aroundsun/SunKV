#include "PersistenceOrchestrator.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <filesystem>

#include <sys/stat.h>

#include "../../network/logger.h"
#include "SnapshotReader.h"
#include "SnapshotWriter.h"
#include "WalReader.h"
#include "WalWriter.h"
#include "../engine/StorageEngine.h"
#include "WalCodec.h"

namespace sunkv::storage2 {

namespace fs = std::filesystem;

namespace {

/// 在 recoverInto 首尾各打一条 WAL 的 bytes/inode，用于核对恢复过程是否改动了同一 WAL 文件。
struct WalRecoverSizeProbe {
    std::string path;

    explicit WalRecoverSizeProbe(std::string p) : path(std::move(p)) { log_("before recoverInto"); }

    ~WalRecoverSizeProbe() { log_("after recoverInto"); }

    void log_(const char* when) const {
        if (path.empty()) {
            return;
        }
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) {
            LOG_INFO("[storage2.recover] wal probe ({}): path='{}' stat_failed errno={}", when, path, errno);
            return;
        }
        if (!S_ISREG(st.st_mode)) {
            LOG_INFO("[storage2.recover] wal probe ({}): path='{}' not_regular st_mode=0{:o}", when, path,
                     static_cast<unsigned>(st.st_mode));
            return;
        }
        LOG_INFO("[storage2.recover] wal probe ({}): path='{}' bytes={} inode={}", when, path,
                 static_cast<uint64_t>(st.st_size), static_cast<uint64_t>(st.st_ino));
    }
};

} // namespace

PersistenceOrchestrator::PersistenceOrchestrator() : PersistenceOrchestrator(Options{}) {}

PersistenceOrchestrator::PersistenceOrchestrator(Options opt) : opt_(opt) {
    if (opt_.enable_wal && !opt_.wal_path.empty()) {
        const size_t max_wal_bytes = opt_.max_wal_file_size_mb > 0
                                         ? static_cast<size_t>(opt_.max_wal_file_size_mb) * 1024 * 1024
                                         : 0;
        wal_writer_ = std::make_unique<WalWriter>(opt_.wal_path, max_wal_bytes);
    }
    if (opt_.async) {
        worker_.emplace([this] { workerLoop_(); });
    }
    if (opt_.enable_snapshot && opt_.snapshot_interval_seconds > 0 && opt_.engine_for_snapshot &&
        !opt_.snapshot_path.empty()) {
        snapshot_interval_running_.store(true);
        snapshot_thread_ = std::thread([this] { snapshotIntervalLoop_(); });
    }
}

PersistenceOrchestrator::~PersistenceOrchestrator() {
    stopPeriodicSnapshot();
    stop_.store(true);
    cv_.notify_all();
    if (worker_.has_value() && worker_->joinable()) {
        worker_->join();
    }
    flush();
}

bool PersistenceOrchestrator::takeSnapshotNow() {
    if (!opt_.engine_for_snapshot || opt_.snapshot_path.empty()) {
        return false;
    }
    try {
        auto records = opt_.engine_for_snapshot->dumpAllLiveRecords();
        return SnapshotWriter::writeToFile(records, opt_.snapshot_path);
    } catch (const std::exception& e) {
        LOG_ERROR("[storage2.snapshot] takeSnapshotNow failed: {}", e.what());
        return false;
    }
}

void PersistenceOrchestrator::stopPeriodicSnapshot() {
    snapshot_interval_running_.store(false);
    if (snapshot_thread_.joinable()) {
        snapshot_thread_.join();
    }
}

void PersistenceOrchestrator::snapshotIntervalLoop_() {
    while (snapshot_interval_running_.load()) {
        const int sec = std::max(1, opt_.snapshot_interval_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(sec));
        if (!snapshot_interval_running_.load()) {
            break;
        }
        if (!takeSnapshotNow()) {
            LOG_WARN("[storage2.snapshot] periodic snapshot failed (will retry)");
        }
    }
}

void PersistenceOrchestrator::addConsumer(MutationConsumer consumer) {
    std::lock_guard<std::mutex> lock(mu_);
    consumers_.push_back(std::move(consumer));
}

void PersistenceOrchestrator::submit(MutationBatch batch) {
    if (batch.empty()) {
        return;
    }
    if (!opt_.async) {
        if (wal_writer_) {
            (void)wal_writer_->append(batch);
        }
        // 同步：直接消费
        std::vector<MutationConsumer> consumers_copy;
        {
            std::lock_guard<std::mutex> lock(mu_);
            consumers_copy = consumers_;
        }
        for (auto& c : consumers_copy) {
            c(batch);
        }
        if (wal_writer_ && opt_.wal_flush_policy == Options::WalFlushPolicy::Always) {
            wal_writer_->flush();
        }
        return;
    }

    const size_t max_queue = opt_.max_queue > 0 ? opt_.max_queue : 1;
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&] { return stop_.load() || queue_.size() < max_queue; });
    if (stop_.load()) {
        dropped_batches_.fetch_add(1);
        return;
    }
    queue_.push_back(std::move(batch));
    cv_.notify_one();
}

void PersistenceOrchestrator::flush() {
    // 语义：尽力把当前已提交的队列 drain 掉，并在启用 WAL 时落盘。
    // 注意：这里只保证本进程内 submit() 已入队的数据被处理；不处理并发写入竞态下的严格一致性。
    std::vector<MutationBatch> local;
    std::vector<MutationConsumer> consumers_copy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!queue_.empty()) {
            local.swap(queue_);
            cv_.notify_all();
        }
        consumers_copy = consumers_;
    }

    if (!local.empty()) {
        if (wal_writer_) {
            std::vector<uint8_t> buf;
            buf.reserve(opt_.wal_group_commit_max_bytes > 0 ? opt_.wal_group_commit_max_bytes : 64 * 1024);
            size_t muts_in_buf = 0;
            auto flushBuf = [&] {
                if (!buf.empty()) {
                    (void)wal_writer_->appendBytes(buf.data(), buf.size());
                    buf.clear();
                    muts_in_buf = 0;
                }
            };
            const size_t max_muts = opt_.wal_group_commit_max_mutations > 0 ? opt_.wal_group_commit_max_mutations : 1;
            const size_t max_bytes = opt_.wal_group_commit_max_bytes > 0 ? opt_.wal_group_commit_max_bytes : 1;

            for (auto& b : local) {
                for (auto& m : b) {
                    const size_t before = buf.size();
                    WalCodec::appendMutation(buf, m);
                    const size_t after = buf.size();
                    ++muts_in_buf;
                    if ((muts_in_buf >= max_muts) || (after > max_bytes && before > 0)) {
                        flushBuf();
                    }
                }
            }
            flushBuf();
        }
        for (const auto& b : local) {
            for (auto& c : consumers_copy) c(b);
        }
    }

    if (wal_writer_) wal_writer_->flush();
}

bool PersistenceOrchestrator::recoverInto(StorageEngine& engine) {
    LOG_INFO("[storage2.recover] begin (snapshot_path='{}', wal_path='{}')",
             opt_.snapshot_path, opt_.wal_path);
    const WalRecoverSizeProbe wal_size_probe{opt_.wal_path};

    // 1) Snapshot
    if (!opt_.snapshot_path.empty()) {
        if (fs::exists(opt_.snapshot_path)) {
            uintmax_t snap_size = 0;
            std::error_code ec;
            snap_size = fs::file_size(opt_.snapshot_path, ec);
            if (ec) snap_size = 0;
            LOG_INFO("[storage2.recover] snapshot exists: path='{}' bytes={}", opt_.snapshot_path, snap_size);
            std::vector<std::pair<std::string, Record>> records;
            if (!SnapshotReader::readFromFile(opt_.snapshot_path, &records)) {
                LOG_ERROR("[storage2.recover] snapshot parse failed: path='{}'", opt_.snapshot_path);
                return false;
            }
            LOG_INFO("[storage2.recover] snapshot parsed: records={}", records.size());
            engine.loadSnapshot(records);
        } else {
            LOG_INFO("[storage2.recover] snapshot not found: path='{}'", opt_.snapshot_path);
        }
    }

    // 2) WAL replay
    if (!opt_.wal_path.empty()) {
        if (!fs::exists(opt_.wal_path)) {
            LOG_INFO("[storage2.recover] wal not found: path='{}'", opt_.wal_path);
            return true;
        }
        std::vector<Mutation> muts;
        if (!WalReader::readAllMutationsWalChain(opt_.wal_path, &muts)) {
            LOG_ERROR("[storage2.recover] wal parse failed: base_path='{}'", opt_.wal_path);
            return false;
        }
        LOG_INFO("[storage2.recover] wal chain parsed: decoded_mutations={}", muts.size());

        size_t applied = 0;
        for (const auto& m : muts) {
            if (!engine.applyMutation(m)) {
                LOG_ERROR("[storage2.recover] wal applyMutation failed at index={} key='{}' type={}",
                          applied, m.key, static_cast<int>(m.type));
                return false;
            }
            applied += 1;
        }
        LOG_INFO("[storage2.recover] wal replay applied: mutations={}", applied);
    }
    LOG_INFO("[storage2.recover] done");
    return true;
}

void PersistenceOrchestrator::workerLoop_() {
    using clock = std::chrono::steady_clock;
    auto last_flush = clock::now();

    while (!stop_.load()) {
        std::vector<MutationBatch> local;
        std::vector<MutationConsumer> consumers_copy;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(50), [&] { return stop_.load() || !queue_.empty(); });
            if (stop_.load()) break;

            // linger：拿到第一批后，短暂等待合并更多提交
            if (!queue_.empty() && opt_.wal_group_commit_linger_ms > 0) {
                cv_.wait_for(lock, std::chrono::milliseconds(opt_.wal_group_commit_linger_ms),
                             [&] { return stop_.load(); });
                if (stop_.load()) break;
            }
            if (!queue_.empty()) local.swap(queue_);
            if (!local.empty()) {
                // 队列已被本轮搬空，唤醒可能因满队列而阻塞的 submit 线程。
                cv_.notify_all();
            }
            consumers_copy = consumers_;
        }

        if (local.empty()) {
            continue;
        }

        // group commit：把这一轮的 batch 合并并按阈值切分写入 WAL（按编码后的 bytes 切分）
        if (wal_writer_) {
            std::vector<uint8_t> buf;
            buf.reserve(opt_.wal_group_commit_max_bytes > 0 ? opt_.wal_group_commit_max_bytes : 64 * 1024);

            size_t muts_in_buf = 0;
            auto flushBuf = [&] {
                if (!buf.empty()) {
                    (void)wal_writer_->appendBytes(buf.data(), buf.size());
                    buf.clear();
                    muts_in_buf = 0;
                }
            };

            const size_t max_muts = opt_.wal_group_commit_max_mutations > 0 ? opt_.wal_group_commit_max_mutations : 1;
            const size_t max_bytes = opt_.wal_group_commit_max_bytes > 0 ? opt_.wal_group_commit_max_bytes : 1;

            for (auto& b : local) {
                for (auto& m : b) {
                    const size_t before = buf.size();
                    WalCodec::appendMutation(buf, m);
                    const size_t after = buf.size();
                    ++muts_in_buf;

                    // 单条就超过 max_bytes：也允许（至少写出去）；否则按阈值切分
                    if ((muts_in_buf >= max_muts) || (after > max_bytes && before > 0)) {
                        flushBuf();
                    }
                }
            }
            flushBuf();
        }

        for (const auto& b : local) {
            for (auto& c : consumers_copy) c(b);
        }

        if (wal_writer_ && opt_.wal_flush_policy == Options::WalFlushPolicy::Always) {
            wal_writer_->flush();
        } else if (wal_writer_ && opt_.wal_flush_policy == Options::WalFlushPolicy::Periodic) {
            auto now = clock::now();
            const auto interval = std::chrono::milliseconds(
                opt_.wal_flush_interval_ms > 0 ? opt_.wal_flush_interval_ms : 1000);
            if (now - last_flush >= interval) {
                wal_writer_->flush();
                last_flush = now;
            }
        }
    }
}

} // namespace sunkv::storage2

