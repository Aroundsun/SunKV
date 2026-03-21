#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <chrono>
#include "WAL.h"
#include "Snapshot.h"

// 数据恢复状态
enum class RecoveryStatus {
    SUCCESS,
    NO_SNAPSHOT,
    SNAPSHOT_CORRUPTED,
    WAL_CORRUPTED,
    INCONSISTENT_DATA,
    IO_ERROR
};

// 恢复选项
struct RecoveryOptions {
    bool enable_snapshot_recovery = true;
    bool enable_wal_recovery = true;
    bool enable_consistency_check = true;
    bool skip_corrupted_entries = true;
    size_t max_recovery_attempts = 3;
    
    // 快照目录
    std::string snapshot_dir = "./snapshots";
    
    // WAL 目录
    std::string wal_dir = "./wal";
    
    // 恢复回调
    std::function<void(const std::string&)> progress_callback;
    std::function<void(const std::string&)> error_callback;
};

// 恢复统计信息
struct RecoveryStats {
    size_t snapshot_entries_loaded = 0;
    size_t wal_entries_replayed = 0;
    size_t corrupted_entries_skipped = 0;
    size_t consistency_errors = 0;
    std::chrono::milliseconds recovery_time{0};
    
    // 文件信息
    std::string snapshot_file;
    std::vector<std::string> wal_files;
    
    // 错误信息
    std::vector<std::string> errors;
};

// 数据恢复器
class DataRecovery {
public:
    DataRecovery(const RecoveryOptions& options = RecoveryOptions{});
    ~DataRecovery();
    
    // 主要恢复接口
    RecoveryStatus recover_data(std::map<std::string, std::string>& data);
    
    // 获取恢复统计
    RecoveryStats get_stats() const;
    
    // 验证数据一致性
    bool verify_consistency(const std::map<std::string, std::string>& data);
    
    // 清理损坏文件
    bool cleanup_corrupted_files();
    
private:
    RecoveryOptions options_;
    RecoveryStats stats_;
    
    // 内部恢复方法
    RecoveryStatus load_from_snapshot(std::map<std::string, std::string>& data);
    RecoveryStatus replay_wal(std::map<std::string, std::string>& data);
    
    // 快照恢复
    bool find_latest_snapshot(std::string& snapshot_file);
    RecoveryStatus load_snapshot_file(const std::string& file, std::map<std::string, std::string>& data);
    
    // WAL 恢复
    std::vector<std::string> find_wal_files();
    RecoveryStatus replay_wal_file(const std::string& file, std::map<std::string, std::string>& data);
    
    // 一致性检查
    bool check_data_integrity(const std::map<std::string, std::string>& data);
    bool check_file_integrity(const std::string& file);
    
    // 错误处理
    void report_error(const std::string& error);
    void report_progress(const std::string& progress);
    
    // 工具方法
    std::string get_recovery_timestamp();
    void update_stats();
};
