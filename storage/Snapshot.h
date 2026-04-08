/**
 * @file Snapshot.h
 * @brief SunKV 快照管理系统
 * 
 * 本文件定义快照系统，提供：
 * - 数据库时间点快照能力
 * - 高效二进制快照格式
 * - 可选压缩以优化存储占用
 * - 自动快照清理与轮转
 * - 多类型数据支持
 * 
 * 快照用于快速恢复某一时刻的数据状态，与 WAL 回放形成互补。
 */

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
#include "../common/DataValue.h"  // 数据值类型定义

// 快照文件格式定义
#define SNAPSHOT_MAGIC_NUMBER 0x544E5250  // "PRNT"
#define SNAPSHOT_VERSION 1

/**
 * @enum SnapshotEntryType
 * @brief 快照中可存储的条目类型
 * 
 * 该枚举定义快照文件中可出现的条目类别。
 */
enum class SnapshotEntryType : uint8_t {
    DATA = 1,           ///< 数据条目（键值对）
    DELETED = 2,        ///< 键删除标记
    TTL = 3,            ///< 键的 TTL 信息
    METADATA = 4         ///< 元数据条目
};

/**
 * @struct SnapshotHeader
 * @brief 快照文件头结构
 * 
 * 该结构包含快照文件元信息，如魔数、版本、时间戳和条目数。
 */
#pragma pack(push, 1)
struct SnapshotHeader {
    uint32_t magic_number;     ///< 魔数 "PRNT"
    uint32_t version;          ///< 快照格式版本
    uint64_t timestamp;        ///< 创建时间戳（微秒）
    uint64_t sequence_number;   ///< 序列号（用于排序）
    uint32_t entry_count;      ///< 快照总条目数
    uint32_t checksum;         ///< 文件头校验和
};
#pragma pack(pop)

/**
 * @struct SnapshotEntryHeader
 * @brief 单条快照记录头结构
 * 
 * 该结构保存每条快照记录的元信息，包括类型、长度、TTL、时间戳与校验和。
 */
#pragma pack(push, 1)
struct SnapshotEntryHeader {
    uint8_t type;            ///< 条目类型（SnapshotEntryType）
    uint32_t key_length;      ///< 键长度（字节）
    uint32_t value_length;     ///< 值长度（字节）
    int64_t ttl_ms;          ///< TTL（毫秒），-1 表示不过期
    uint64_t timestamp;       ///< 条目时间戳（微秒）
    uint32_t checksum;        ///< 条目校验和
};
#pragma pack(pop)

/**
 * @class SnapshotEntry
 * @brief 表示单条快照记录
 * 
 * 该类封装快照条目的类型、键、值、TTL、时间戳及校验信息。
 */
class SnapshotEntry {
public:
    SnapshotEntryType type;   ///< 条目类型
    std::string key;          ///< 键
    std::string value;        ///< 值（用于数据条目）
    int64_t ttl_ms;           ///< TTL（毫秒），-1 表示不过期
    uint64_t timestamp;       ///< 条目时间戳
    uint32_t checksum;        ///< 完整性校验和
    
    /**
     * @brief 默认构造函数
     */
    SnapshotEntry();
    
    /**
     * @brief 带参数构造函数
     * @param t 条目类型
     * @param k 键
     * @param v 值
     * @param ttl TTL（毫秒）
     */
    SnapshotEntry(SnapshotEntryType t, const std::string& k, const std::string& v, int64_t ttl);
    
    /**
     * @brief 将条目序列化为二进制数据
     * @return 序列化后的字节数组
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * @brief 从二进制数据反序列化条目
     * @param data 待反序列化的二进制数据
     * @return 反序列化后的条目智能指针
     */
    static std::unique_ptr<SnapshotEntry> deserialize(const std::vector<uint8_t>& data);
    
    /**
     * @brief 计算条目校验和
     * @return 计算得到的校验和值
     */
    uint32_t calculate_checksum() const;
    
    /**
     * @brief 校验条目完整性
     * @return 校验通过返回 true，否则返回 false
     */
    bool verify_checksum() const;
    
    /**
     * @brief 获取序列化条目大小
     * @return 字节大小
     */
    size_t size() const;
    
private:
    /**
     * @brief 更新条目校验和
     */
    void update_checksum();
};

/**
 * @class SnapshotWriter
 * @brief 快照写入器
 * 
 * 该类负责将快照条目写入文件，并维护文件头与写入统计信息。
 */
class SnapshotWriter {
public:
    /**
     * @brief 构造 SnapshotWriter
     * @param file_path 快照文件路径
     */
    SnapshotWriter(const std::string& file_path);
    
    /**
     * @brief 析构函数
     */
    ~SnapshotWriter();
    
    /**
     * @brief 打开快照文件用于写入
     * @return 成功返回 true，否则返回 false
     */
    bool open();
    
    /**
     * @brief 关闭快照文件
     */
    void close();
    
    /**
     * @brief 写入数据条目
     * @param key 键
     * @param value 值
     * @param ttl_ms TTL（毫秒），-1 表示不过期
     * @return 成功返回 true，否则返回 false
     */
    bool write_data(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    /**
     * @brief 写入删除标记条目
     * @param key 要标记删除的键
     * @return 成功返回 true，否则返回 false
     */
    bool write_deleted(const std::string& key);
    
    /**
     * @brief 写入元数据条目
     * @param key 元数据键
     * @param value 元数据值
     * @return 成功返回 true，否则返回 false
     */
    bool write_metadata(const std::string& key, const std::string& value);
    
    /**
     * @brief 将缓冲数据刷入磁盘
     * @return 成功返回 true，否则返回 false
     */
    bool flush();
    
    /**
     * @brief 写入统计信息
     */
    struct WriteStats {
        size_t entries_written;    ///< 写入总条目数
        size_t bytes_written;      ///< 写入总字节数
        size_t data_entries;        ///< 数据条目数
        size_t deleted_entries;     ///< 删除标记条目数
        size_t metadata_entries;    ///< 元数据条目数
    };
    
    /**
     * @brief Get write statistics
     * @return WriteStats structure with current statistics
     */
    WriteStats get_stats() const;
    
private:
    std::string file_path_;                     ///< Path to the snapshot file
    std::ofstream file_stream_;                  ///< Output file stream
    std::mutex mutex_;                          ///< Mutex for thread safety
    std::atomic<size_t> entries_written_{0};     ///< Total entries written
    std::atomic<size_t> bytes_written_{0};       ///< Total bytes written
    std::atomic<size_t> data_entries_{0};        ///< Data entries written
    std::atomic<size_t> deleted_entries_{0};     ///< Deleted entries written
    std::atomic<size_t> metadata_entries_{0};    ///< Metadata entries written
    uint64_t sequence_number_{0};                ///< Current sequence number
    uint64_t start_timestamp_;                   ///< Snapshot creation timestamp
    
    /**
     * @brief Write an entry to the snapshot
     * @param entry The entry to write
     * @return True if successful, false otherwise
     */
    bool write_entry(const SnapshotEntry& entry);
    
    /**
     * @brief Write the snapshot header
     */
    void write_header();
    
    /**
     * @brief Update the snapshot header
     */
    void update_header();
};

/**
 * @class SnapshotReader
 * @brief Handles reading snapshot data from files
 * 
 * This class provides functionality to read snapshot entries
 * from disk with proper header parsing and statistics tracking.
 */
class SnapshotReader {
public:
    /**
     * @brief Constructor for SnapshotReader
     * @param file_path Path to the snapshot file
     */
    SnapshotReader(const std::string& file_path);
    
    /**
     * @brief Destructor
     */
    ~SnapshotReader();
    
    /**
     * @brief Open the snapshot file for reading
     * @return True if successful, false otherwise
     */
    bool open();
    
    /**
     * @brief Close the snapshot file
     */
    void close();
    
    /**
     * @brief Read the next entry from the snapshot
     * @return Unique pointer to the next entry, or nullptr if EOF
     */
    std::unique_ptr<SnapshotEntry> read_next_entry();
    
    /**
     * @brief Check if end of file has been reached
     * @return True if EOF, false otherwise
     */
    bool eof() const;
    
    /**
     * @brief Reset reading position to the beginning of the file
     */
    void reset();
    
    /**
     * @brief Get the current file position
     * @return Position in bytes from the beginning
     */
    size_t get_position() const;
    
    /**
     * @brief Statistics for read operations
     */
    struct ReadStats {
        size_t entries_read;       ///< Total entries read
        size_t bytes_read;         ///< Total bytes read
        size_t valid_entries;      ///< Valid entries read
        size_t invalid_entries;    ///< Invalid entries read
        size_t data_entries;        ///< Data entries read
        size_t deleted_entries;     ///< Deleted entries read
        size_t metadata_entries;    ///< Metadata entries read
    };
    
    /**
     * @brief Get read statistics
     * @return ReadStats structure with current statistics
     */
    ReadStats get_stats() const;
    
    /**
     * @brief Snapshot information structure
     */
    struct SnapshotInfo {
        uint64_t timestamp;        ///< Snapshot timestamp
        uint64_t sequence_number;   ///< Snapshot sequence number
        uint32_t entry_count;      ///< Number of entries in snapshot
    };
    
    /**
     * @brief Get snapshot information
     * @return SnapshotInfo structure with snapshot metadata
     */
    SnapshotInfo get_snapshot_info() const;
    
private:
    std::string file_path_;                     ///< Path to the snapshot file
    std::ifstream file_stream_;                  ///< Input file stream
    mutable std::mutex mutex_;                  ///< Mutex for thread safety
    std::atomic<size_t> entries_read_{0};        ///< Total entries read
    std::atomic<size_t> bytes_read_{0};          ///< Total bytes read
    std::atomic<size_t> valid_entries_{0};       ///< Valid entries read
    std::atomic<size_t> invalid_entries_{0};     ///< Invalid entries read
    std::atomic<size_t> data_entries_{0};        ///< Data entries read
    std::atomic<size_t> deleted_entries_{0};     ///< Deleted entries read
    std::atomic<size_t> metadata_entries_{0};    ///< Metadata entries read
    
    SnapshotHeader header_;                      ///< Snapshot header
    bool header_read_;                           ///< Whether header has been read
    
    /**
     * @brief Read the snapshot header
     * @return True if successful, false otherwise
     */
    bool read_header();
    
    /**
     * @brief Read data from buffer
     * @param bytes Number of bytes to read
     * @param data Buffer to store data
     * @return True if successful, false otherwise
     */
    bool read_from_buffer(size_t bytes, std::vector<uint8_t>& data);
};

/**
 * @class SnapshotManager
 * @brief High-level snapshot management system
 * 
 * This class provides a unified interface for managing snapshots,
 * including creation, loading, compression, and cleanup.
 */
class SnapshotManager {
public:
    /**
     * @brief Constructor for SnapshotManager
     * @param snapshot_dir Directory to store snapshots
     * @param max_file_size Maximum size of each snapshot file (default: 100MB)
     */
    SnapshotManager(const std::string& snapshot_dir, size_t max_file_size = 100 * 1024 * 1024);
    
    /**
     * @brief Destructor
     */
    ~SnapshotManager();
    
    /**
     * @brief Initialize the snapshot manager
     * @return True if successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Create a snapshot from string data
     * @param data The data to snapshot
     * @return True if successful, false otherwise
     */
    bool create_snapshot(const std::map<std::string, std::string>& data);
    
    /**
     * @brief Create a snapshot from multi-type data
     * @param data The multi-type data to snapshot
     * @return True if successful, false otherwise
     */
    bool create_multi_type_snapshot(const std::map<std::string, DataValue>& data);
    
    /**
     * @brief Load a snapshot into string data
     * @param data The data to load into
     * @return True if successful, false otherwise
     */
    bool load_snapshot(std::map<std::string, std::string>& data);
    
    /**
     * @brief Get the latest snapshot file path
     * @return Path to the latest snapshot file
     */
    std::string get_latest_snapshot() const;
    
    /**
     * @brief Clean up old snapshots
     * @param keep_count Number of snapshots to keep (default: 5)
     */
    void cleanup_old_snapshots(size_t keep_count = 5);
    
    /**
     * @brief Statistics for snapshot operations
     */
    struct SnapshotStats {
        size_t total_snapshots;      ///< Total number of snapshots
        size_t total_size;          ///< Total size of all snapshots
        size_t latest_size;          ///< Size of the latest snapshot
        std::string latest_snapshot;  ///< Path to the latest snapshot
    };
    
    /**
     * @brief Get snapshot statistics
     * @return SnapshotStats structure with current statistics
     */
    SnapshotStats get_stats() const;
    
private:
    std::string snapshot_dir_;                     ///< Directory to store snapshots
    size_t max_file_size_;                        ///< Maximum size of each snapshot file
    
    mutable std::mutex mutex_;                     ///< Mutex for thread safety
    std::atomic<uint64_t> current_sequence_{0};    ///< Current sequence number
    
    /**
     * @brief Create snapshot directory if it doesn't exist
     * @return True if successful, false otherwise
     */
    bool create_snapshot_directory();
    
    /**
     * @brief List all snapshot files
     * @return Vector of snapshot file paths
     */
    std::vector<std::string> list_snapshot_files() const;
    
    /**
     * @brief Generate snapshot filename based on current sequence
     * @return Generated filename
     */
    std::string generate_snapshot_filename() const;
    
    /**
     * @brief Compress a snapshot file
     * @param input_file Path to input file
     * @param output_file Path to output file
     * @return True if successful, false otherwise
     */
    bool compress_snapshot(const std::string& input_file, const std::string& output_file);
    
    /**
     * @brief Decompress a snapshot file
     * @param input_file Path to input file
     * @param output_file Path to output file
     * @return True if successful, false otherwise
     */
    bool decompress_snapshot(const std::string& input_file, const std::string& output_file);
};
