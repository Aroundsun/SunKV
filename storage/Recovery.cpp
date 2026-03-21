#include "Recovery.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

DataRecovery::DataRecovery(const RecoveryOptions& options) 
    : options_(options) {
}

DataRecovery::~DataRecovery() {
}

RecoveryStatus DataRecovery::recover_data(std::map<std::string, std::string>& data) {
    auto start_time = std::chrono::steady_clock::now();
    
    report_progress("开始数据恢复...");
    
    // 清空目标数据
    data.clear();
    stats_ = RecoveryStats{};
    
    // 第一步：从快照恢复
    if (options_.enable_snapshot_recovery) {
        report_progress("正在从快照恢复数据...");
        auto snapshot_status = load_from_snapshot(data);
        
        if (snapshot_status != RecoveryStatus::SUCCESS && 
            snapshot_status != RecoveryStatus::NO_SNAPSHOT) {
            report_error("快照恢复失败: " + std::to_string(static_cast<int>(snapshot_status)));
            return snapshot_status;
        }
        
        if (snapshot_status == RecoveryStatus::NO_SNAPSHOT) {
            report_progress("未找到快照文件，将从 WAL 开始恢复");
        } else {
            report_progress("快照恢复完成，加载了 " + 
                          std::to_string(stats_.snapshot_entries_loaded) + " 个条目");
        }
    }
    
    // 第二步：重放 WAL
    if (options_.enable_wal_recovery) {
        report_progress("正在重放 WAL 日志...");
        auto wal_status = replay_wal(data);
        
        if (wal_status != RecoveryStatus::SUCCESS) {
            report_error("WAL 重放失败: " + std::to_string(static_cast<int>(wal_status)));
            return wal_status;
        }
        
        report_progress("WAL 重放完成，处理了 " + 
                      std::to_string(stats_.wal_entries_replayed) + " 个条目");
    }
    
    // 第三步：一致性检查
    if (options_.enable_consistency_check) {
        report_progress("正在进行数据一致性检查...");
        if (!verify_consistency(data)) {
            report_error("数据一致性检查失败");
            return RecoveryStatus::INCONSISTENT_DATA;
        }
        report_progress("数据一致性检查通过");
    }
    
    // 计算恢复时间
    auto end_time = std::chrono::steady_clock::now();
    stats_.recovery_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    report_progress("数据恢复完成！耗时: " + std::to_string(stats_.recovery_time.count()) + "ms");
    
    return RecoveryStatus::SUCCESS;
}

RecoveryStats DataRecovery::get_stats() const {
    return stats_;
}

bool DataRecovery::verify_consistency(const std::map<std::string, std::string>& data) {
    return check_data_integrity(data);
}

bool DataRecovery::cleanup_corrupted_files() {
    bool cleaned = false;
    
    // 清理损坏的快照文件
    if (std::filesystem::exists(options_.snapshot_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(options_.snapshot_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".snap") {
                if (!check_file_integrity(entry.path().string())) {
                    report_error("删除损坏的快照文件: " + entry.path().string());
                    std::filesystem::remove(entry.path());
                    cleaned = true;
                }
            }
        }
    }
    
    // 清理损坏的 WAL 文件
    if (std::filesystem::exists(options_.wal_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(options_.wal_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log") {
                if (!check_file_integrity(entry.path().string())) {
                    report_error("删除损坏的 WAL 文件: " + entry.path().string());
                    std::filesystem::remove(entry.path());
                    cleaned = true;
                }
            }
        }
    }
    
    return cleaned;
}

RecoveryStatus DataRecovery::load_from_snapshot(std::map<std::string, std::string>& data) {
    std::string snapshot_file;
    
    // 查找最新的快照文件
    if (!find_latest_snapshot(snapshot_file)) {
        return RecoveryStatus::NO_SNAPSHOT;
    }
    
    stats_.snapshot_file = snapshot_file;
    
    // 加载快照文件
    return load_snapshot_file(snapshot_file, data);
}

RecoveryStatus DataRecovery::replay_wal(std::map<std::string, std::string>& data) {
    auto wal_files = find_wal_files();
    
    if (wal_files.empty()) {
        report_progress("未找到 WAL 文件");
        return RecoveryStatus::SUCCESS;
    }
    
    stats_.wal_files = wal_files;
    
    // 按时间顺序重放 WAL 文件
    for (const auto& wal_file : wal_files) {
        auto status = replay_wal_file(wal_file, data);
        if (status != RecoveryStatus::SUCCESS) {
            return status;
        }
    }
    
    return RecoveryStatus::SUCCESS;
}

bool DataRecovery::find_latest_snapshot(std::string& snapshot_file) {
    if (!std::filesystem::exists(options_.snapshot_dir)) {
        return false;
    }
    
    std::vector<std::string> snapshot_files;
    
    // 查找所有快照文件
    for (const auto& entry : std::filesystem::directory_iterator(options_.snapshot_dir)) {
        if (entry.is_regular_file() && 
            entry.path().filename().string().find("snapshot_") == 0 &&
            entry.path().extension() == ".snap") {
            snapshot_files.push_back(entry.path().string());
        }
    }
    
    if (snapshot_files.empty()) {
        return false;
    }
    
    // 按文件名排序，获取最新的
    std::sort(snapshot_files.begin(), snapshot_files.end());
    snapshot_file = snapshot_files.back();
    
    return true;
}

RecoveryStatus DataRecovery::load_snapshot_file(const std::string& file, std::map<std::string, std::string>& data) {
    try {
        SnapshotReader reader(file);
        if (!reader.open()) {
            return RecoveryStatus::SNAPSHOT_CORRUPTED;
        }
        
        size_t loaded_count = 0;
        
        while (!reader.eof()) {
            auto entry = reader.read_next_entry();
            if (!entry) {
                continue;
            }
            
            switch (entry->type) {
                case SnapshotEntryType::DATA:
                    data[entry->key] = entry->value;
                    loaded_count++;
                    break;
                case SnapshotEntryType::DELETED:
                    data.erase(entry->key);
                    loaded_count++;
                    break;
                case SnapshotEntryType::METADATA:
                    // 暂时忽略元数据
                    break;
                case SnapshotEntryType::TTL:
                    // 暂时忽略 TTL
                    break;
            }
        }
        
        stats_.snapshot_entries_loaded = loaded_count;
        return RecoveryStatus::SUCCESS;
        
    } catch (const std::exception& e) {
        report_error("加载快照文件异常: " + std::string(e.what()));
        return RecoveryStatus::SNAPSHOT_CORRUPTED;
    }
}

std::vector<std::string> DataRecovery::find_wal_files() {
    std::vector<std::string> wal_files;
    
    if (!std::filesystem::exists(options_.wal_dir)) {
        return wal_files;
    }
    
    // 查找所有 WAL 文件
    for (const auto& entry : std::filesystem::directory_iterator(options_.wal_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            wal_files.push_back(entry.path().string());
        }
    }
    
    // 按文件名排序（按时间顺序）
    std::sort(wal_files.begin(), wal_files.end());
    
    return wal_files;
}

RecoveryStatus DataRecovery::replay_wal_file(const std::string& file, std::map<std::string, std::string>& data) {
    try {
        WALReader reader(file);
        if (!reader.open()) {
            if (options_.skip_corrupted_entries) {
                report_error("跳过损坏的 WAL 文件: " + file);
                return RecoveryStatus::SUCCESS;
            }
            return RecoveryStatus::WAL_CORRUPTED;
        }
        
        size_t replayed_count = 0;
        size_t corrupted_count = 0;
        
        while (!reader.eof()) {
            auto entry = reader.read_next_entry();
            if (!entry) {
                continue;
            }
            
            try {
                switch (entry->operation) {
                    case WALOperationType::SET:
                        data[entry->key] = entry->value;
                        replayed_count++;
                        break;
                    case WALOperationType::DEL:
                        data.erase(entry->key);
                        replayed_count++;
                        break;
                    case WALOperationType::CLEAR:
                        data.clear();
                        replayed_count++;
                        break;
                    case WALOperationType::BEGIN:
                    case WALOperationType::COMMIT:
                    case WALOperationType::ROLLBACK:
                        // 事务操作，暂时忽略
                        replayed_count++;
                        break;
                }
            } catch (const std::exception& e) {
                corrupted_count++;
                if (!options_.skip_corrupted_entries) {
                    report_error("WAL 条目处理失败: " + std::string(e.what()));
                    return RecoveryStatus::WAL_CORRUPTED;
                }
            }
        }
        
        stats_.wal_entries_replayed += replayed_count;
        stats_.corrupted_entries_skipped += corrupted_count;
        
        return RecoveryStatus::SUCCESS;
        
    } catch (const std::exception& e) {
        report_error("重放 WAL 文件异常: " + std::string(e.what()));
        return RecoveryStatus::WAL_CORRUPTED;
    }
}

bool DataRecovery::check_data_integrity(const std::map<std::string, std::string>& data) {
    // 基本的数据完整性检查
    for (const auto& pair : data) {
        // 检查键是否为空
        if (pair.first.empty()) {
            report_error("发现空键");
            return false;
        }
        
        // 检查键长度是否合理
        if (pair.first.length() > 1024) {
            report_error("键长度过长: " + pair.first.substr(0, 50) + "...");
            return false;
        }
        
        // 检查值长度是否合理
        if (pair.second.length() > 1024 * 1024) {  // 1MB
            report_error("值长度过长: " + pair.first);
            return false;
        }
    }
    
    return true;
}

bool DataRecovery::check_file_integrity(const std::string& file) {
    // 基本的文件完整性检查
    try {
        if (!std::filesystem::exists(file)) {
            return false;
        }
        
        auto file_size = std::filesystem::file_size(file);
        if (file_size == 0) {
            return false;
        }
        
        // 尝试打开文件
        std::ifstream stream(file, std::ios::binary);
        if (!stream.is_open()) {
            return false;
        }
        
        // 检查文件是否可读
        char buffer[1024];
        stream.read(buffer, sizeof(buffer));
        if (stream.bad() && !stream.eof()) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception&) {
        return false;
    }
}

void DataRecovery::report_error(const std::string& error) {
    stats_.errors.push_back(error);
    if (options_.error_callback) {
        options_.error_callback(error);
    }
}

void DataRecovery::report_progress(const std::string& progress) {
    if (options_.progress_callback) {
        options_.progress_callback(progress);
    }
}

std::string DataRecovery::get_recovery_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

void DataRecovery::update_stats() {
    // 更新统计信息的占位符
}
