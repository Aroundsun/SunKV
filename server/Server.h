#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <string>
#include <unordered_map>
#include "../network/TcpServer.h"
#include "../network/TcpConnection.h"
#include "../network/EventLoop.h"
#include "../protocol/RESPType.h"
#include "../protocol/RESPParser.h"
#include "../common/Config.h"    // 配置系统
#include "../storage2/Factory.h"

struct ArrayCmdDispatchCtx;

/**
 * @brief SunKV 服务器主类
 * 
 * 负责整合所有模块，提供完整的键值存储服务
 * 包括网络处理、协议解析、命令执行、数据存储和持久化
 */
class Server {
    friend struct ArrayCmdDispatchCtx;

public:
    /**
     * @brief 构造函数
     * @param config 配置对象
     */
    explicit Server(const Config& config);
    
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
    
    /**
     * @brief 设置停止标志（用于信号处理）
     */
    void setStopping() { stopping_.store(true); }
    
    /**
     * @brief 检查服务器是否停止
     */
    bool isStopping() const { return stopping_.load(); }

    /// EXPIRE 等命令使用的最大 TTL（秒）
    int maxTtlSeconds() const { return config_.max_ttl_seconds; }
    
    /**
     * @brief 停止主事件循环（用于信号处理）
     */
    void stopMainLoop() { if (main_loop_) main_loop_->quit(); }

private:
    enum class ShutdownPhase : int {
        NotStarted = 0,
        Requested = 1,
        ThreadsStopped = 2,
        GracefulDone = 3,
        Completed = 4,
    };

    void requestStopFromSignal();
    void advanceShutdown(ShutdownPhase target);

    /**
     * @brief 初始化网络层
     */
    bool initializeNetwork();
    
    /**
     * @brief 初始化存储层
     */
    bool initializeStorage();
    
    /**
     * @brief 创建多数据类型快照
     */
    bool create_multi_type_snapshot();
    

    
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
    
    /**
     * @brief 周期统计日志线程函数
     */
    void statsReportThread();

    /// 周期性快照（enable_snapshot 且 snapshot_interval_seconds>0）
    void snapshotIntervalThread();
    
    /**
     * @brief 构建统计信息文本
     */
    std::string buildStatsReport();

private:
    /// RESP 数组命令表驱动（首参数为命令名）；返回 true 表示已处理。
    bool dispatchArrayCommand_(const std::shared_ptr<TcpConnection>& conn,
                               const std::string& cmd_name,
                               const std::vector<RESPValue::Ptr>& cmd_array);

    Config config_;                    // 配置对象
    std::unique_ptr<EventLoop> main_loop_;              // 主事件循环
    std::unique_ptr<TcpServer> tcp_server_;             // TCP 服务器
    sunkv::storage2::Storage2Components storage2_;       // v2 存储组件组装结果
    
    struct ConnParseState {
        std::string pending_input;        // 连接的输入缓冲
        RESPParser parser;                // 连接的解析器
    };

    // 每连接解析上下文：持有残留输入与可复用 RESPParser，避免每条命令重复构造解析器。
    std::mutex conn_inbuf_mu_;
    std::unordered_map<std::string, std::shared_ptr<ConnParseState>> conn_inbuf_;
    
    std::atomic<bool> running_{false};                 // 运行状态
    std::atomic<bool> stopping_{false};                // 停止状态
    std::atomic<int> shutdown_phase_{static_cast<int>(ShutdownPhase::NotStarted)};
    
    // TTL 清理相关
    std::thread ttl_cleanup_thread_;                  // TTL 清理线程
    std::atomic<bool> ttl_cleanup_running_{false};     // TTL 清理线程运行状态

    std::thread snapshot_interval_thread_;
    std::atomic<bool> snapshot_interval_running_{false};
    
    // 统计输出线程
    std::thread stats_report_thread_;
    std::atomic<bool> stats_report_running_{false};
    
    // 信号处理转发线程（通过管道安全处理 SIGINT/SIGTERM）
    std::thread signal_thread_;
    std::atomic<bool> signal_thread_running_{false};
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    std::atomic<uint64_t> total_connections_{0};
    std::atomic<uint64_t> current_connections_{0};
    std::atomic<uint64_t> total_commands_{0};
    std::atomic<uint64_t> expired_keys_cleaned_{0};   // 清理的过期键计数
    std::atomic<uint64_t> total_operations_{0};
    std::chrono::steady_clock::time_point start_time_;
    
};
