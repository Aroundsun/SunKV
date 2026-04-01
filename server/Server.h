#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "network/EventLoop.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoopThreadPool.h"
#include "protocol/RESPParser.h"
#include "protocol/RESPSerializer.h"
#include "command/CommandRegistry.h"
#include "storage/StorageEngine.h"
#include "storage/WAL.h"
#include "storage/Snapshot.h"
#include "network/logger.h"

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

private:
    std::unique_ptr<Config> config_;                    // 配置对象
    std::unique_ptr<EventLoop> main_loop_;              // 主事件循环
    std::unique_ptr<TcpServer> tcp_server_;             // TCP 服务器
    std::unique_ptr<EventLoopThreadPool> thread_pool_;    // 线程池
    std::unique_ptr<CommandRegistry> command_registry_; // 命令注册表
    StorageEngine* storage_engine_;                       // 存储引擎
    std::unique_ptr<WALManager> wal_manager_;          // WAL 管理器
    std::unique_ptr<SnapshotManager> snapshot_manager_; // 快照管理器
    
    std::atomic<bool> running_{false};                 // 运行状态
    std::atomic<bool> stopping_{false};                // 停止状态
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> current_connections_{0};
    std::atomic<uint64_t> total_commands_{0};
    std::atomic<uint64_t> total_operations_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 线程相关
    std::thread server_thread_;                           // 服务器线程
    std::vector<std::thread> worker_threads_;              // 工作线程
};
