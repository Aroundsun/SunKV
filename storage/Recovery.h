/**
 * @file Recovery.h
 * @brief SunKV 数据恢复系统
 * 
 * 本文件包含数据恢复系统的实现，提供：
 * - 从快照文件恢复数据
 * - 从 WAL 日志重放操作
 * - 数据一致性检查
 * - 损坏文件处理
 * - 恢复进度跟踪
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <chrono>
#include "WAL.h"
#include "Snapshot.h"

/**
 * @enum RecoveryStatus
 * @brief 数据恢复状态枚举
 * 
 * 定义了数据恢复过程中可能出现的各种状态
 */
enum class RecoveryStatus {
    SUCCESS,              ///< 恢复成功
    NO_SNAPSHOT,          ///< 没有找到快照文件
    SNAPSHOT_CORRUPTED,   ///< 快照文件损坏
    WAL_CORRUPTED,        ///< WAL 文件损坏
    INCONSISTENT_DATA,   ///< 数据不一致
    IO_ERROR              ///< IO 错误
};

/**
 * @struct RecoveryOptions
 * @brief 数据恢复选项配置
 * 
 * 定义了数据恢复过程中的各种配置选项
 */
struct RecoveryOptions {
    bool enable_snapshot_recovery = true;     ///< 是否启用快照恢复
    bool enable_wal_recovery = true;            ///< 是否启用 WAL 恢复
    bool enable_consistency_check = true;        ///< 是否启用一致性检查
    bool skip_corrupted_entries = true;          ///< 是否跳过损坏的条目
    size_t max_recovery_attempts = 3;           ///< 最大恢复尝试次数
    
    /// 快照目录路径
    std::string snapshot_dir = "./snapshots";
    
    /// WAL 目录路径
    std::string wal_dir = "./wal";
    
    /// 恢复进度回调函数
    std::function<void(const std::string&)> progress_callback;
    
    /// 恢复错误回调函数
    std::function<void(const std::string&)> error_callback;
};

/**
 * @struct RecoveryStats
 * @brief 数据恢复统计信息
 * 
 * 记录数据恢复过程中的各种统计信息
 */
struct RecoveryStats {
    size_t snapshot_entries_loaded = 0;        ///< 从快照加载的条目数
    size_t wal_entries_replayed = 0;            ///< 从 WAL 重放的条目数
    size_t corrupted_entries_skipped = 0;       ///< 跳过的损坏条目数
    size_t consistency_errors = 0;             ///< 一致性错误数
    std::chrono::milliseconds recovery_time{0}; ///< 恢复耗时
    
    /// 使用的快照文件
    std::string snapshot_file;
    
    /// 使用的 WAL 文件列表
    std::vector<std::string> wal_files;
    
    /// 错误信息列表
    std::vector<std::string> errors;
};

/**
 * @class DataRecovery
 * @brief 数据恢复器
 * 
 * 提供完整的数据恢复功能，包括从快照和 WAL 文件恢复数据
 */
class DataRecovery {
public:
    /**
     * @brief 构造函数
     * @param options 恢复选项配置
     */
    DataRecovery(const RecoveryOptions& options = RecoveryOptions{});
    
    /**
     * @brief 析构函数
     */
    ~DataRecovery();
    
    /**
     * @brief 主要恢复接口
     * @param data 用于存储恢复数据的映射
     * @return 恢复状态
     */
    RecoveryStatus recover_data(std::map<std::string, std::string>& data);
    
    /**
     * @brief 获取恢复统计信息
     * @return 恢复统计信息
     */
    RecoveryStats get_stats() const;
    
    /**
     * @brief 验证数据一致性
     * @param data 要验证的数据
     * @return 是否一致
     */
    bool verify_consistency(const std::map<std::string, std::string>& data);
    
    /**
     * @brief 清理损坏的文件
     * @return 是否成功清理
     */
    bool cleanup_corrupted_files();
    
private:
    RecoveryOptions options_;     ///< 恢复选项
    RecoveryStats stats_;          ///< 恢复统计信息
    
    /// 内部恢复方法
    RecoveryStatus load_from_snapshot(std::map<std::string, std::string>& data);  ///< 从快照加载数据
    RecoveryStatus replay_wal(std::map<std::string, std::string>& data);          ///< 重放 WAL 日志
    
    /// 快照恢复相关方法
    bool find_latest_snapshot(std::string& snapshot_file);                        ///< 查找最新快照
    RecoveryStatus load_snapshot_file(const std::string& file, std::map<std::string, std::string>& data);  ///< 加载快照文件
    
    /// WAL 恢复相关方法
    std::vector<std::string> find_wal_files();                                    ///< 查找 WAL 文件
    RecoveryStatus replay_wal_file(const std::string& file, std::map<std::string, std::string>& data);      ///< 重放 WAL 文件
    
    /// 一致性检查方法
    bool check_data_integrity(const std::map<std::string, std::string>& data);     ///< 检查数据完整性
    bool check_file_integrity(const std::string& file);                           ///< 检查文件完整性
    
    /// 错误处理方法
    void report_error(const std::string& error);                                   ///< 报告错误
    void report_progress(const std::string& progress);                             ///< 报告进度
    
    /// 工具方法
    std::string get_recovery_timestamp();                                          ///< 获取恢复时间戳
    void update_stats();                                                           ///< 更新统计信息
};
