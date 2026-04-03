#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <atomic>
#include <map>
#include <fstream>
#include "../common/DataValue.h"  // 数据值定义

// Snapshot 文件格式定义
#define SNAPSHOT_MAGIC_NUMBER 0x544E5250  // "PRNT" (Print)
#define SNAPSHOT_VERSION 1

// Snapshot 条目类型
enum class SnapshotEntryType : uint8_t {
    DATA = 1,           // 数据条目
    DELETED = 2,        // 删除标记
    TTL = 3,            // TTL 信息
    METADATA = 4         // 元数据
};

// Snapshot 文件头
#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic_number;     // 魔数 "PRNT"
    uint32_t version;          // 版本号
    uint64_t timestamp;        // 创建时间戳
    uint64_t sequence_number;   // 序列号
    uint32_t entry_count;      // 条目数量
    uint32_t checksum;         // 头部校验和
};
#pragma pack(pop)

// Snapshot 条目头
#pragma pack(push, 1)
struct SnapshotEntryHeader {
    uint8_t type;            // 条目类型
    uint32_t key_length;      // 键长度
    uint32_t value_length;     // 值长度
    int64_t ttl_ms;          // TTL（-1 表示永不过期）
    uint64_t timestamp;       // 时间戳
    uint32_t checksum;        // 条目校验和
};
#pragma pack(pop)

// Snapshot 条目
class SnapshotEntry {
public:
    SnapshotEntryType type;
    std::string key;
    std::string value;
    int64_t ttl_ms;
    uint64_t timestamp;
    uint32_t checksum;
    
    SnapshotEntry();
    SnapshotEntry(SnapshotEntryType t, const std::string& k, const std::string& v, int64_t ttl);
    
    // 序列化为二进制数据
    std::vector<uint8_t> serialize() const;
    
    // 从二进制数据反序列化
    static std::unique_ptr<SnapshotEntry> deserialize(const std::vector<uint8_t>& data);
    
    // 计算校验和
    uint32_t calculate_checksum() const;
    
    // 验证校验和
    bool verify_checksum() const;
    
    // 获取条目大小
    size_t size() const;
    
private:
    void update_checksum();
};

// Snapshot Writer - 负责写入快照文件
class SnapshotWriter {
public:
    SnapshotWriter(const std::string& file_path);
    ~SnapshotWriter();
    
    // 打开快照文件
    bool open();
    
    // 关闭快照文件
    void close();
    
    // 写入数据条目
    bool write_data(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    // 写入删除标记
    bool write_deleted(const std::string& key);
    
    // 写入元数据
    bool write_metadata(const std::string& key, const std::string& value);
    
    // 刷新到磁盘
    bool flush();
    
    // 获取写入统计
    struct WriteStats {
        size_t entries_written;
        size_t bytes_written;
        size_t data_entries;
        size_t deleted_entries;
        size_t metadata_entries;
    };
    
    WriteStats get_stats() const;
    
private:
    std::string file_path_;
    std::ofstream file_stream_;
    std::mutex mutex_;
    std::atomic<size_t> entries_written_{0};
    std::atomic<size_t> bytes_written_{0};
    std::atomic<size_t> data_entries_{0};
    std::atomic<size_t> deleted_entries_{0};
    std::atomic<size_t> metadata_entries_{0};
    uint64_t sequence_number_{0};
    uint64_t start_timestamp_;
    
    bool write_entry(const SnapshotEntry& entry);
    void write_header();
    void update_header();
};

// Snapshot Reader - 负责读取快照文件
class SnapshotReader {
public:
    SnapshotReader(const std::string& file_path);
    ~SnapshotReader();
    
    // 打开快照文件
    bool open();
    
    // 关闭快照文件
    void close();
    
    // 读取下一个条目
    std::unique_ptr<SnapshotEntry> read_next_entry();
    
    // 检查是否到达文件末尾
    bool eof() const;
    
    // 重置到文件开头
    void reset();
    
    // 获取当前文件位置
    size_t get_position() const;
    
    // 获取读取统计
    struct ReadStats {
        size_t entries_read;
        size_t bytes_read;
        size_t valid_entries;
        size_t invalid_entries;
        size_t data_entries;
        size_t deleted_entries;
        size_t metadata_entries;
    };
    
    ReadStats get_stats() const;
    
    // 获取快照信息
    struct SnapshotInfo {
        uint64_t timestamp;
        uint64_t sequence_number;
        uint32_t entry_count;
    };
    
    SnapshotInfo get_snapshot_info() const;
    
private:
    std::string file_path_;
    std::ifstream file_stream_;
    mutable std::mutex mutex_;
    std::atomic<size_t> entries_read_{0};
    std::atomic<size_t> bytes_read_{0};
    std::atomic<size_t> valid_entries_{0};
    std::atomic<size_t> invalid_entries_{0};
    std::atomic<size_t> data_entries_{0};
    std::atomic<size_t> deleted_entries_{0};
    std::atomic<size_t> metadata_entries_{0};
    
    SnapshotHeader header_;
    bool header_read_;
    
    bool read_header();
    bool read_from_buffer(size_t bytes, std::vector<uint8_t>& data);
};

// Snapshot Manager - 统一管理快照
class SnapshotManager {
public:
    SnapshotManager(const std::string& snapshot_dir, size_t max_file_size = 100 * 1024 * 1024);
    ~SnapshotManager();
    
    // 初始化快照管理器
    bool initialize();
    
    // 创建快照
    bool create_snapshot(const std::map<std::string, std::string>& data);
    
    // 创建多数据类型快照
    bool create_multi_type_snapshot(const std::map<std::string, DataValue>& data);
    
    // 加载快照
    bool load_snapshot(std::map<std::string, std::string>& data);
    
    // 获取最新快照信息
    std::string get_latest_snapshot() const;
    
    // 清理旧快照
    void cleanup_old_snapshots(size_t keep_count = 5);
    
    // 获取统计信息
    struct SnapshotStats {
        size_t total_snapshots;
        size_t total_size;
        size_t latest_size;
        std::string latest_snapshot;
    };
    
    SnapshotStats get_stats() const;
    
private:
    std::string snapshot_dir_;
    size_t max_file_size_;
    
    mutable std::mutex mutex_;
    std::atomic<uint64_t> current_sequence_{0};
    
    bool create_snapshot_directory();
    std::vector<std::string> list_snapshot_files() const;
    std::string generate_snapshot_filename() const;
    bool compress_snapshot(const std::string& input_file, const std::string& output_file);
    bool decompress_snapshot(const std::string& input_file, const std::string& output_file);
};
