#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <fstream>
#include <atomic>
#include <functional>
#include <map>

class StorageEngine;  // 前向声明

// 前向声明 DataValue
struct DataValue;

// WAL 操作类型
enum class WALOperationType : uint8_t {
    SET = 1,      // 设置键值对
    DEL = 2,      // 删除键
    CLEAR = 3,    // 清空所有数据
    BEGIN = 4,    // 开始事务
    COMMIT = 5,   // 提交事务
    ROLLBACK = 6  // 回滚事务
};

// WAL 日志条目
struct WALLogEntry {
    uint64_t sequence_number;    // 序列号
    uint64_t timestamp;          // 时间戳（微秒）
    WALOperationType operation;   // 操作类型
    std::string key;             // 键
    std::string value;           // 值
    int64_t ttl_ms;              // TTL（毫秒），-1 表示永不过期
    uint32_t checksum;           // 校验和
    
    WALLogEntry() : sequence_number(0), timestamp(0), operation(WALOperationType::SET), ttl_ms(-1), checksum(0) {}
    
    // 序列化为字节
    std::vector<uint8_t> serialize() const;
    
    // 从字节反序列化
    static std::unique_ptr<WALLogEntry> deserialize(const std::vector<uint8_t>& data);
    
    // 计算校验和
    uint32_t calculate_checksum() const;
    
    // 验证校验和
    bool verify_checksum() const;
    
    // 获取条目大小
    size_t size() const;
};

// WAL 写入器
class WALWriter {
public:
    explicit WALWriter(const std::string& file_path, size_t buffer_size = 64 * 1024);
    ~WALWriter();
    
    // 禁用拷贝
    WALWriter(const WALWriter&) = delete;
    WALWriter& operator=(const WALWriter&) = delete;
    
    // 打开 WAL 文件
    bool open();
    
    // 关闭 WAL 文件
    void close();
    
    // 写入日志条目
    bool write_entry(const WALLogEntry& entry);
    
    // 刷新到磁盘
    bool flush();
    
    // 获取当前序列号
    uint64_t get_sequence_number() const { return sequence_number_; }
    
    // 获取文件大小
    size_t get_file_size() const;
    
    // 检查是否已打开
    bool is_open() const { return file_stream_.is_open(); }
    
    // 设置同步模式（每次写入都同步到磁盘）
    void set_sync_mode(bool sync) { sync_mode_ = sync; }
    
    // 获取统计信息
    struct WriteStats {
        uint64_t total_entries;
        uint64_t total_bytes;
        uint64_t flush_count;
        uint64_t sync_count;
    };
    
    WriteStats get_stats() const;

private:
    std::string file_path_;
    std::ofstream file_stream_;
    size_t buffer_size_;
    std::vector<uint8_t> write_buffer_;
    std::atomic<uint64_t> sequence_number_;
    bool sync_mode_;
    
    // 确保互斥锁正确对齐
    alignas(std::mutex) mutable std::mutex mutex_;
    std::atomic<bool> destructing_{false};  // 析构标志
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_;
    mutable std::atomic<uint64_t> total_bytes_;
    mutable std::atomic<uint64_t> flush_count_;
    mutable std::atomic<uint64_t> sync_count_;
    
    // 内部写入方法
    bool write_to_buffer(const WALLogEntry& entry);
    bool flush_buffer();
    void sync_to_disk();
};

// WAL 读取器
class WALReader {
public:
    explicit WALReader(const std::string& file_path);
    ~WALReader();
    
    // 禁用拷贝
    WALReader(const WALReader&) = delete;
    WALReader& operator=(const WALReader&) = delete;
    
    // 打开 WAL 文件
    bool open();
    
    // 关闭 WAL 文件
    void close();
    
    // 读取下一个日志条目
    std::unique_ptr<WALLogEntry> read_next_entry();
    
    // 重置到文件开头
    void reset();
    
    // 跳转到指定序列号
    bool seek_to_sequence(uint64_t sequence);
    
    // 检查是否已打开
    bool is_open() const { return file_stream_.is_open(); }
    
    // 检查是否到达文件末尾
    bool eof() const;
    
    // 获取当前位置
    size_t get_position() const;
    
    // 设置读取回调（用于批量处理）
    using EntryCallback = std::function<bool(const WALLogEntry&)>;
    bool read_all_entries(EntryCallback callback);
    
    // 获取统计信息
    struct ReadStats {
        uint64_t total_entries;
        uint64_t valid_entries;
        uint64_t invalid_entries;
        uint64_t total_bytes;
    };
    
    ReadStats get_stats() const;

private:
    std::string file_path_;
    std::ifstream file_stream_;
    size_t buffer_size_;
    std::vector<uint8_t> read_buffer_;
    
    mutable std::mutex mutex_;
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_;
    mutable std::atomic<uint64_t> valid_entries_;
    mutable std::atomic<uint64_t> invalid_entries_;
    mutable std::atomic<uint64_t> total_bytes_;
    
    // 内部读取方法
    bool read_from_buffer(size_t bytes, std::vector<uint8_t>& data);
    bool read_entry_header(WALLogEntry& entry);
    bool read_entry_data(WALLogEntry& entry);
};

// WAL 管理器
class WALManager {
public:
    explicit WALManager(const std::string& wal_dir, size_t max_file_size = 100 * 1024 * 1024);
    ~WALManager();
    
    // 禁用拷贝
    WALManager(const WALManager&) = delete;
    WALManager& operator=(const WALManager&) = delete;
    
    // 初始化 WAL
    bool initialize();
    
    // 写入 SET 操作
    bool write_set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    // 写入 DEL 操作
    bool write_del(const std::string& key);
    
    // 写入 CLEAR 操作
    bool write_clear();
    
    // 开始事务
    bool begin_transaction();
    
    // 提交事务
    bool commit_transaction();
    
    // 回滚事务
    bool rollback_transaction();
    
    // 刷新到磁盘
    bool flush();
    
    // 重放 WAL（用于恢复）
    bool replay(StorageEngine& storage);
    
    // 重放 WAL（用于多数据类型恢复）
    bool replay_multi_type(std::map<std::string, DataValue>& storage);
    
    // 清理旧的 WAL 文件
    bool cleanup_old_files();
    
    // 获取当前序列号
    uint64_t get_sequence_number() const;
    
    // 获取统计信息
    struct WALStats {
        uint64_t total_entries;
        uint64_t total_files;
        size_t total_size;
        uint64_t write_ops;
        uint64_t read_ops;
        uint64_t flush_ops;
    };
    
    WALStats get_stats() const;

private:
    std::string wal_dir_;
    size_t max_file_size_;
    uint64_t current_sequence_;
    bool in_transaction_;
    
    mutable std::mutex mutex_;
    std::atomic<bool> destructing_{false};  // 析构标志
    std::unique_ptr<WALWriter> current_writer_;
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_;
    mutable std::atomic<uint64_t> total_files_;
    mutable std::atomic<uint64_t> total_size_;
    mutable std::atomic<uint64_t> write_ops_;
    mutable std::atomic<uint64_t> read_ops_;
    mutable std::atomic<uint64_t> flush_ops_;
    
    // 内部方法
    std::string get_current_wal_file() const;
    std::string get_wal_file_path(uint64_t sequence) const;
    bool rotate_wal_file();
    std::vector<std::string> list_wal_files() const;
    bool create_wal_directory();
};
