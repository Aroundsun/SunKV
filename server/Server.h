#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <string>
#include <unordered_map>
#include <cstdint>
#include "../network/TcpServer.h"
#include "../network/TcpConnection.h"
#include "../network/EventLoop.h"
#include "../protocol/RESPType.h"
#include "../protocol/RESPParser.h"
#include "common/Config.h"
#include "storage2/Factory.h"

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
    auto start() -> bool;
    
    /**
     * @brief 停止服务器
     */
    auto stop() -> void;
    
    /**
     * @brief 检查服务器是否正在运行
     * @return 运行状态
     */
    auto isRunning() const -> bool { return running_.load(); }
    
    /**
     * @brief 等待服务器停止
     */
    auto waitForStop() -> void;
    
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
    
    auto getStats() const -> ServerStats;
    
    /**
     * @brief 设置停止标志（用于信号处理）
     */
    auto setStopping() -> void { stopping_.store(true); }
    
    /**
     * @brief 检查服务器是否停止
     */
    auto isStopping() const -> bool { return stopping_.load(); }

    /// EXPIRE 等命令使用的最大 TTL（秒）
    auto maxTtlSeconds() const -> int { return config_.max_ttl_seconds; }
    
    /**
     * @brief 停止主事件循环（用于信号处理）
     */
    auto stopMainLoop() -> void {
        if (main_loop_) {
            main_loop_->quit();
        }
    }

private:
    enum class ShutdownPhase : std::uint8_t {
        NotStarted = 0,
        Requested = 1,
        ThreadsStopped = 2,
        GracefulDone = 3,
        Completed = 4,
    };

    auto requestStopFromSignal() -> void;
    auto advanceShutdown(ShutdownPhase target) -> void;

    /**
     * @brief 初始化网络层
     */
    auto initializeNetwork() -> bool;
    
    /**
     * @brief 初始化存储层
     */
    auto initializeStorage() -> bool;
    
    /**
     * @brief 创建多数据类型快照
     */
    auto create_multi_type_snapshot() const -> bool;
    

    
    /**
     * @brief 设置连接回调
     */
    auto setupConnectionCallbacks() -> void;
    
    /**
     * @brief 连接建立回调
     */
    auto onConnection(const std::shared_ptr<TcpConnection>& conn) -> void;
    
    /**
     * @brief 消息接收回调
     */
    auto onMessage(const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) -> void;
    
    /**
     * @brief 连接关闭回调
     */
    auto onDisconnection(const std::shared_ptr<TcpConnection>& conn) -> void;
    
    /**
     * @brief 处理 RESP 命令
     */
    auto processCommand(const std::shared_ptr<TcpConnection>& conn,
                        const RESPValue::Ptr& command) -> void;
    /**
     * @brief 优雅关闭服务器
     */
    auto gracefulShutdown() -> void;
    
    /**
     * @brief 更新统计信息
     */
    auto updateStats() -> void;
    
    /**
     * @brief TTL 清理线程函数
     */
    auto ttlCleanupThread() -> void;
    
    /**
     * @brief 清理过期的键
     */
    auto cleanupExpiredKeys() -> void;
    
    /**
     * @brief 周期统计日志线程函数
     */
    auto statsReportThread() -> void;

    
    /**
     * @brief 构建统计信息文本
     */
    auto buildStatsReport() -> std::string;

    /// RESP 数组命令表驱动（首参数为命令名）；返回 true 表示已处理。
    auto dispatchArrayCommand_(const std::shared_ptr<TcpConnection>& conn,
                               const std::string& cmd_name,
                               const std::vector<RESPValue::Ptr>& cmd_array) -> bool;

    Config config_;                    // 配置对象
    std::unique_ptr<EventLoop> main_loop_;              // 主事件循环
    std::unique_ptr<TcpServer> tcp_server_;             // TCP 服务器
    sunkv::storage2::Storage2Components storage2_;       // v2 存储组件组装结果
    
    struct ConnParseState {
        std::string pending_input;        // 连接的输入缓冲
        size_t pending_offset{0};         // 已消费偏移（避免频繁 erase(0, n)）
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
