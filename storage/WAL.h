/**
 * @file WAL.h
 * @brief SunKV 的预写日志（WAL）实现
 * 
 * 本文件定义 WAL 系统，提供：
 * - 面向持久化的原子预写日志能力
 * - 崩溃恢复能力
 * - 事务记录能力
 * - 高性能缓冲 I/O
 * - 日志文件滚动与清理
 * 
 * WAL 通过“先写日志再改内存”的方式保证数据持久性。
 */

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

// DataValue 前向声明
struct DataValue;

/**
 * @enum WALOperationType
 * @brief 可记录到 WAL 的操作类型
 * 
 * 该枚举定义 WAL 中可落盘的所有操作类别。
 */
enum class WALOperationType : uint8_t {
    SET = 1,      ///< 设置键值对
    DEL = 2,      ///< 删除键
    CLEAR = 3,    ///< 清空数据
    BEGIN = 4,    ///< 开始事务
    COMMIT = 5,   ///< 提交事务
    ROLLBACK = 6  ///< 回滚事务
};

/**
 * @struct WALLogEntry
 * @brief WAL 中的一条日志记录
 * 
 * 该结构包含恢复时重放一次数据库操作所需的完整信息。
 */
struct WALLogEntry {
    uint64_t sequence_number;    ///< 单调递增序列号
    uint64_t timestamp;          ///< 时间戳（自 Unix 纪元起的微秒数）
    WALOperationType operation;   ///< 操作类型
    std::string key;             ///< 被操作的键
    std::string value;           ///< 值（主要用于 SET）
    int64_t ttl_ms;              ///< 生存时间（毫秒），-1 表示不过期
    uint32_t checksum;           ///< 数据完整性校验和
    
    /**
     * @brief 默认构造函数
     * 使用默认值初始化一条 WAL 日志记录
     */
    WALLogEntry() : sequence_number(0), timestamp(0), operation(WALOperationType::SET), ttl_ms(-1), checksum(0) {}
    
    /**
     * @brief 将日志记录序列化为字节数组
     * @return 序列化后的字节表示
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * @brief 从字节数组反序列化日志记录
     * @param data 包含序列化内容的字节数组
     * @return 反序列化后的 WALLogEntry 智能指针
     */
    static std::unique_ptr<WALLogEntry> deserialize(const std::vector<uint8_t>& data);
    
    /**
     * @brief 计算日志记录校验和
     * @return 计算得到的校验和值
     */
    uint32_t calculate_checksum() const;
    
    /**
     * @brief 校验日志记录完整性
     * @return 校验通过返回 true，否则返回 false
     */
    bool verify_checksum() const;
    
    /**
     * @brief 获取序列化后大小
     * @return 序列化数据字节数
     */
    size_t size() const;
};

/**
 * @class WALWriter
 * @brief 负责带缓冲地写入 WAL 日志
 * 
 * 该类通过缓冲 I/O 降低磁盘写入次数，提高 WAL 写入性能。
 */
class WALWriter {
public:
    /**
     * @brief 构造 WALWriter
     * @param file_path WAL 文件路径
     * @param buffer_size 写缓冲大小（默认 64KB）
     */
    explicit WALWriter(const std::string& file_path, size_t buffer_size = 64 * 1024);
    
    /**
     * @brief 析构函数（确保缓冲数据落盘）
     */
    ~WALWriter();
    
    // 禁用拷贝语义
    WALWriter(const WALWriter&) = delete;
    WALWriter& operator=(const WALWriter&) = delete;
    
    /**
     * @brief 打开 WAL 文件用于写入
     * @return 成功返回 true，否则返回 false
     */
    bool open();
    
    /**
     * @brief 关闭 WAL 文件
     */
    void close();
    
    /**
     * @brief 写入一条 WAL 日志
     * @param entry 待写入的日志记录
     * @return 成功返回 true，否则返回 false
     */
    bool write_entry(const WALLogEntry& entry);
    
    /**
     * @brief 将缓冲数据刷入磁盘
     * @return 成功返回 true，否则返回 false
     */
    bool flush();
    
    /**
     * @brief 获取当前序列号
     * @return 下一条日志将使用的序列号
     */
    uint64_t get_sequence_number() const { return sequence_number_; }
    
    /**
     * @brief 获取当前文件大小
     * @return 文件字节数
     */
    size_t get_file_size() const;
    
    /**
     * @brief 判断 WAL 文件是否已打开
     * @return 已打开返回 true，否则返回 false
     */
    bool is_open() const { return file_stream_.is_open(); }
    
    /**
     * @brief 设置同步模式（每次写入后同步到磁盘）
     * @param sync 是否开启同步模式
     */
    void set_sync_mode(bool sync) { sync_mode_ = sync; }
    
    /**
     * @brief 写入统计信息
     */
    struct WriteStats {
        uint64_t total_entries;  ///< 总写入条目数
        uint64_t total_bytes;    ///< 总写入字节数
        uint64_t flush_count;    ///< flush 次数
        uint64_t sync_count;     ///< sync 次数
    };
    
    /**
     * @brief 获取写入统计
     * @return 当前写入统计结构
     */
    WriteStats get_stats() const;

private:
    std::string file_path_;               ///< WAL 文件路径
    std::ofstream file_stream_;            ///< 输出文件流
    size_t buffer_size_;                   ///< 写缓冲大小
    std::vector<uint8_t> write_buffer_;    ///< 批量写缓冲区
    std::atomic<uint64_t> sequence_number_; ///< 下一序列号
    bool sync_mode_;                       ///< 是否每次写入后同步
    
    // 保证互斥锁对齐
    alignas(std::mutex) mutable std::mutex mutex_;
    std::atomic<bool> destructing_{false};  ///< 析构中标记
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_; ///< 总写入条目数
    mutable std::atomic<uint64_t> total_bytes_;   ///< 总写入字节数
    mutable std::atomic<uint64_t> flush_count_;   ///< flush 次数
    mutable std::atomic<uint64_t> sync_count_;    ///< sync 次数
    
    // 内部写入方法
    bool write_to_buffer(const WALLogEntry& entry);  ///< 追加日志到缓冲区
    bool flush_buffer();                             ///< 将缓冲区写入磁盘
    void sync_to_disk();                             ///< 执行磁盘同步
};

/**
 * @class WALReader
 * @brief 负责从磁盘读取 WAL 日志
 * 
 * 该类提供高效 WAL 顺序读取能力，并支持按序列号定位。
 */
class WALReader {
public:
    /**
     * @brief 构造 WALReader
     * @param file_path 待读取的 WAL 文件路径
     */
    explicit WALReader(const std::string& file_path);
    
    /**
     * @brief 析构函数
     */
    ~WALReader();
    
    // 禁用拷贝语义
    WALReader(const WALReader&) = delete;
    WALReader& operator=(const WALReader&) = delete;
    
    /**
     * @brief 打开 WAL 文件用于读取
     * @return 成功返回 true，否则返回 false
     */
    bool open();
    
    /**
     * @brief 关闭 WAL 文件
     */
    void close();
    
    /**
     * @brief 读取下一条日志记录
     * @return 下一条日志指针；到达 EOF 返回 nullptr
     */
    std::unique_ptr<WALLogEntry> read_next_entry();
    
    /**
     * @brief 将读取位置重置到文件开头
     */
    void reset();
    
    /**
     * @brief 定位到指定序列号
     * @param sequence 目标序列号
     * @return 定位成功返回 true，未找到返回 false
     */
    bool seek_to_sequence(uint64_t sequence);
    
    /**
     * @brief 判断 WAL 文件是否已打开
     * @return 已打开返回 true，否则返回 false
     */
    bool is_open() const { return file_stream_.is_open(); }
    
    /**
     * @brief 判断是否到达文件末尾
     * @return 到达 EOF 返回 true，否则返回 false
     */
    bool eof() const;
    
    /**
     * @brief 获取当前读取位置
     * @return 相对文件开头的字节偏移
     */
    size_t get_position() const;
    
    /**
     * @brief 批处理回调函数类型
     * @param entry 当前处理的日志记录
     * @return 返回 true 继续处理，返回 false 停止处理
     */
    using EntryCallback = std::function<bool(const WALLogEntry&)>;
    
    /**
     * @brief 读取全部日志并逐条回调处理
     * @param callback 每条日志对应的处理回调
     * @return 全部处理成功返回 true，否则返回 false
     */
    bool read_all_entries(EntryCallback callback);
    
    /**
     * @brief 读取统计信息
     */
    struct ReadStats {
        uint64_t total_entries;   ///< 总读取条目数
        uint64_t valid_entries;   ///< 校验通过条目数
        uint64_t invalid_entries; ///< 校验失败条目数
        uint64_t total_bytes;     ///< 总读取字节数
    };
    
    /**
     * @brief 获取读取统计
     * @return 当前读取统计结构
     */
    ReadStats get_stats() const;

private:
    std::string file_path_;               ///< WAL 文件路径
    std::ifstream file_stream_;            ///< 输入文件流
    size_t buffer_size_;                   ///< 读缓冲大小
    std::vector<uint8_t> read_buffer_;     ///< 批量读缓冲区
    
    mutable std::mutex mutex_;             ///< 线程安全互斥锁
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_;   ///< 总读取条目数
    mutable std::atomic<uint64_t> valid_entries_;   ///< 校验通过条目数
    mutable std::atomic<uint64_t> invalid_entries_; ///< 校验失败条目数
    mutable std::atomic<uint64_t> total_bytes_;     ///< 总读取字节数
    
    // 内部读取方法
    bool read_from_buffer(size_t bytes, std::vector<uint8_t>& data);  ///< 从缓冲区读取数据
    bool read_entry_header(WALLogEntry& entry);                        ///< 读取日志头
    bool read_entry_data(WALLogEntry& entry);                         ///< 读取日志体
};

/**
 * @class WALManager
 * @brief 高层 WAL 管理器（含滚动与清理）
 * 
 * 该类管理多个 WAL 文件，负责日志滚动、旧文件清理，
 * 并对外提供统一的 WAL 写入与回放接口。
 */
class WALManager {
public:
    /**
     * @brief 构造 WALManager
     * @param wal_dir WAL 文件目录
     * @param max_file_size 单个 WAL 文件最大大小（默认 100MB）
     */
    explicit WALManager(const std::string& wal_dir, size_t max_file_size = 100 * 1024 * 1024);
    
    /**
     * @brief 析构函数
     */
    ~WALManager();
    
    // 禁用拷贝语义
    WALManager(const WALManager&) = delete;
    WALManager& operator=(const WALManager&) = delete;
    
    /**
     * @brief 初始化 WAL 管理器
     * @return 成功返回 true，否则返回 false
     */
    bool initialize();
    
    /**
     * @brief 记录一条 SET 操作
     * @param key 键
     * @param value 值
     * @param ttl_ms 生存时间（毫秒），-1 表示不过期
     * @return 成功返回 true，否则返回 false
     */
    bool write_set(const std::string& key, const std::string& value, int64_t ttl_ms = -1);
    
    /**
     * @brief 记录一条 DEL 操作
     * @param key 待删除键
     * @return 成功返回 true，否则返回 false
     */
    bool write_del(const std::string& key);
    
    /**
     * @brief 记录一条 CLEAR 操作
     * @return 成功返回 true，否则返回 false
     */
    bool write_clear();
    
    /**
     * @brief 记录事务开始
     * @return 成功返回 true，否则返回 false
     */
    bool begin_transaction();
    
    /**
     * @brief 记录事务提交
     * @return 成功返回 true，否则返回 false
     */
    bool commit_transaction();
    
    /**
     * @brief 记录事务回滚
     * @return 成功返回 true，否则返回 false
     */
    bool rollback_transaction();
    
    /**
     * @brief 刷新所有缓冲数据到磁盘
     * @return 成功返回 true，否则返回 false
     */
    bool flush();
    
    /**
     * @brief 回放 WAL 到存储引擎（用于恢复）
     * @param storage 目标存储引擎
     * @return 成功返回 true，否则返回 false
     */
    bool replay(StorageEngine& storage);
    
    /**
     * @brief 回放 WAL 到多类型存储映射（用于恢复）
     * @param storage 目标多类型存储映射
     * @return 成功返回 true，否则返回 false
     */
    bool replay_multi_type(std::map<std::string, DataValue>& storage);
    
    /**
     * @brief 清理旧 WAL 文件
     * @return 成功返回 true，否则返回 false
     */
    bool cleanup_old_files();
    
    /**
     * @brief 获取当前序列号
     * @return 下一条日志将使用的序列号
     */
    uint64_t get_sequence_number() const;
    
    /**
     * @brief WAL 统计信息
     */
    struct WALStats {
        uint64_t total_entries;  ///< WAL 总条目数
        uint64_t total_files;    ///< WAL 文件总数
        size_t total_size;       ///< WAL 文件总大小
        uint64_t write_ops;      ///< 写操作次数
        uint64_t read_ops;       ///< 读操作次数
        uint64_t flush_ops;      ///< 刷盘次数
    };
    
    /**
     * @brief 获取 WAL 统计
     * @return 当前 WAL 统计结构
     */
    WALStats get_stats() const;

private:
    std::string wal_dir_;                          ///< WAL 文件目录
    size_t max_file_size_;                         ///< 单个 WAL 文件最大大小
    uint64_t current_sequence_;                    ///< 当前序列号
    bool in_transaction_;                          ///< 当前是否处于事务中
    
    mutable std::mutex mutex_;                    ///< 线程安全互斥锁
    std::atomic<bool> destructing_{false};        ///< 析构中标记
    std::unique_ptr<WALWriter> current_writer_;   ///< 当前 WAL 写入器实例
    
    // 统计信息
    mutable std::atomic<uint64_t> total_entries_;  ///< 总写入条目数
    mutable std::atomic<uint64_t> total_files_;    ///< 创建的 WAL 文件总数
    mutable std::atomic<uint64_t> total_size_;     ///< WAL 文件总大小
    mutable std::atomic<uint64_t> write_ops_;      ///< 写操作次数
    mutable std::atomic<uint64_t> read_ops_;       ///< 读操作次数
    mutable std::atomic<uint64_t> flush_ops_;      ///< 刷盘次数
    
    // 内部方法
    std::string get_current_wal_file() const;                     ///< 获取当前 WAL 文件名
    std::string get_wal_file_path(uint64_t sequence) const;       ///< 按序列号生成 WAL 文件路径
    bool rotate_wal_file();                                       ///< 滚动到新 WAL 文件
    std::vector<std::string> list_wal_files() const;              ///< 列出所有 WAL 文件
    bool create_wal_directory();                                  ///< 按需创建 WAL 目录
};
