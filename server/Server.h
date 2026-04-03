#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <set>
#include <unordered_map>
#include <list>
#include <functional>
#include <chrono>
#include <iostream>
#include "../network/TcpServer.h"
#include "../network/TcpConnection.h"
#include "../network/EventLoop.h"
#include "../network/EventLoopThreadPool.h"
#include "../command/CommandRegistry.h"
#include "../storage/StorageEngine.h"
#include "../storage/WAL.h"
#include "../storage/Snapshot.h"
#include "../protocol/RESPType.h"
#include "../protocol/RESPSerializer.h"
#include "../protocol/RESPParser.h"
#include "../network/logger.h"

// 数据类型枚举
enum class DataType {
    STRING = 0,
    LIST = 1,
    SET = 2,
    HASH = 3
};

// TTL 相关常量
constexpr int64_t NO_TTL = -1;  // 永不过期
constexpr int64_t TTL_EXPIRED = -2;  // 已过期

// 多类型值结构
struct DataValue {
    DataType type;
    std::string string_value;                    // STRING 类型
    std::list<std::string> list_value;          // LIST 类型
    std::set<std::string> set_value;            // SET 类型
    std::map<std::string, std::string> hash_value; // HASH 类型
    
    // TTL 支持
    int64_t ttl_seconds;                         // TTL 秒数，-1 表示永不过期
    std::chrono::steady_clock::time_point created_time;  // 创建时间
    std::chrono::steady_clock::time_point ttl_set_time;   // TTL 设置时间
    
    DataValue() : type(DataType::STRING), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::string& val) : type(DataType::STRING), string_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::list<std::string>& val) : type(DataType::LIST), list_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::set<std::string>& val) : type(DataType::SET), set_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    explicit DataValue(const std::map<std::string, std::string>& val) : type(DataType::HASH), hash_value(val), ttl_seconds(NO_TTL), created_time(std::chrono::steady_clock::now()), ttl_set_time(created_time) {}
    
    // 检查是否过期
    bool isExpired() const {
        if (ttl_seconds == NO_TTL) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ttl_set_time);
        return elapsed.count() >= ttl_seconds;
    }
    
    // 获取剩余 TTL 秒数
    int64_t getRemainingTTL() const {
        if (ttl_seconds == NO_TTL) {
            return NO_TTL;
        }
        if (isExpired()) {
            return TTL_EXPIRED;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ttl_set_time);
        return ttl_seconds - elapsed.count();
    }
    
    // 设置 TTL
    void setTTL(int64_t ttl) {
        ttl_seconds = ttl;
        if (ttl > 0) {
            ttl_set_time = std::chrono::steady_clock::now();
        }
    }
};

// 前向声明
class Config;

/**
 * @brief SunKV 服务器主类
 * 
 * 负责整合所有模块，提供完整的键值存储服务
 * 包括网络处理、协议解析、命令执行、数据存储和持久化
 */
class Server {
public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     */
    explicit Server(std::unique_ptr<Config> config);
    
    /**
     * @brief 析构函数
     */
    ~Server();
    
    // 禁用拷贝
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    
    /**
     * @brief 启动服务器
     * @return 是否启动成功
     */
    bool start();
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 检查服务器是否正在运行
     * @return 运行状态
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief 等待服务器停止
     */
    void waitForStop();
    
    /**
     * @brief 获取服务器统计信息
     */
    struct ServerStats {
        uint64_t total_connections;
        uint64_t current_connections;
        uint64_t total_commands;
        uint64_t total_operations;
        uint64_t uptime_seconds;
    };
    
    ServerStats getStats() const;

private:
    /**
     * @brief 初始化网络层
     */
    bool initializeNetwork();
    
    /**
     * @brief 初始化存储层
     */
    bool initializeStorage();
    
    /**
     * @brief 初始化持久化层
     */
    bool initializePersistence();
    
    /**
     * @brief 加载多数据类型快照
     */
    bool load_multi_type_snapshot(std::map<std::string, DataValue>& data);
    
    /**
     * @brief 创建多数据类型快照
     */
    bool create_multi_type_snapshot();
    
    /**
     * @brief 初始化命令系统
     */
    bool initializeCommands();
    
    /**
     * @brief 设置连接回调
     */
    void setupConnectionCallbacks();
    
    /**
     * @brief 连接建立回调
     */
    void onConnection(const std::shared_ptr<TcpConnection>& conn);
    
    /**
     * @brief 消息接收回调
     */
    void onMessage(const std::shared_ptr<TcpConnection>& conn, void* data, size_t len);
    
    /**
     * @brief 连接关闭回调
     */
    void onDisconnection(const std::shared_ptr<TcpConnection>& conn);
    
    /**
     * @brief 处理 RESP 命令
     */
    void processCommand(const std::shared_ptr<TcpConnection>& conn, 
                    const RESPValue::Ptr& command);
    /**
     * @brief 发送响应
     * @param conn 连接对象
     * @param result 命令执行结果
     */
    void sendResponse(const std::shared_ptr<TcpConnection>& conn, const CommandResult& result);
    
    /**
     * @brief 优雅关闭服务器
     */
    void gracefulShutdown();
    
    /**
     * @brief 更新统计信息
     */
    void updateStats();
    
    /**
     * @brief TTL 清理线程函数
     */
    void ttlCleanupThread();
    
    /**
     * @brief 清理过期的键
     */
    void cleanupExpiredKeys();

private:
    std::unique_ptr<Config> config_;                    // 配置对象
    std::unique_ptr<EventLoop> main_loop_;              // 主事件循环
    std::unique_ptr<TcpServer> tcp_server_;             // TCP 服务器
    std::unique_ptr<EventLoopThreadPool> thread_pool_;    // 线程池
    std::unique_ptr<CommandRegistry> command_registry_; // 命令注册表
    StorageEngine* storage_engine_;                       // 存储引擎
    std::unique_ptr<WALManager> wal_manager_;          // WAL 管理器
    std::unique_ptr<SnapshotManager> snapshot_manager_; // 快照管理器
    
    // 多类型内存存储
    std::map<std::string, DataValue> multi_storage_;
    std::mutex multi_storage_mutex_;
    
    // 快照时间戳（用于 WAL 过滤）
    uint64_t snapshot_timestamp_{0};
    
    // 简单内存存储 (临时实现，向后兼容)
    std::map<std::string, std::string> simple_storage_;
    std::mutex simple_storage_mutex_;
    
    std::atomic<bool> running_{false};                 // 运行状态
    std::atomic<bool> stopping_{false};                // 停止状态
    
    // TTL 清理相关
    std::thread ttl_cleanup_thread_;                  // TTL 清理线程
    std::atomic<bool> ttl_cleanup_running_{false};     // TTL 清理线程运行状态
    static constexpr int TTL_CLEANUP_INTERVAL_SECONDS = 5;  // 清理间隔（秒）
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> current_connections_{0};
    std::atomic<uint64_t> total_commands_{0};
    std::atomic<uint64_t> expired_keys_cleaned_{0};   // 清理的过期键计数
    std::atomic<uint64_t> total_operations_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 线程相关
    std::thread server_thread_;                           // 服务器线程
    std::vector<std::thread> worker_threads_;              // 工作线程
};
