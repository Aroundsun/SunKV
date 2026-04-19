#include "Server.h"
#include "ArrayCmdDispatch.h"
#include "../common/Config.h"
#include <csignal>
#include <sstream>
#include <unistd.h>
#include <filesystem>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include "../network/Buffer.h"
#include "../common/MemoryPool.h"
#include "../protocol/RESPSerializer.h"
#include "../protocol/RESPParser.h"
#include "../storage2/Factory.h"
#include "../storage2/persistence/PersistenceOrchestrator.h"
#include "../storage2/persistence/SnapshotWriter.h"

namespace {

static sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy parseWalFlushPolicyString(const std::string& raw) {
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    if (s == "never") {
        return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Never;
    }
    if (s == "always") {
        return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Always;
    }
    return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Periodic;
}

} // namespace

// 全局服务器实例，用于信号处理
static Server* g_server = nullptr;
static int pipe_fds[2] = {-1, -1};

/**
 * @brief 信号处理函数
 */
void signalHandler(int signal) {
    if (g_server && pipe_fds[1] >= 0) {
        // 信号处理函数中仅做异步信号安全操作：写管道通知
        uint8_t sig = static_cast<uint8_t>(signal);
        (void)!write(pipe_fds[1], &sig, sizeof(sig));
    }
}

Server::Server(const Config& config)
    : config_(config),
      start_time_(std::chrono::steady_clock::now()) {
    
    // 创建管道用于信号处理
    if (pipe(pipe_fds) == -1) {
        LOG_ERROR("pipe() 失败: {}", strerror(errno));
        return;
    }
    
    // 设置全局服务器实例
    g_server = this;
    
    // 设置信号处理
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // kill 命令
    std::signal(SIGPIPE, SIG_IGN);    // 忽略管道信号
}

Server::~Server() {
    // 析构只兜底：避免 main/信号线程与析构重复 stop 造成“防重入掩盖问题”
    if (running_.load() || stopping_.load()) {
    stop();
    }
    g_server = nullptr;
    
    // 关闭管道
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

bool Server::start() {
    #ifdef DEBUG
#endif
    LOG_INFO("正在启动 SunKV Server...");
    
    if (running_.load()) {
        LOG_ERROR("服务器已在运行");
        return false;
    }
    
    // 验证配置
    if (!config_.validate()) {
        LOG_ERROR("配置无效");
        return false;
    }
    
    // 创建必要的目录
    if (!std::filesystem::exists(config_.data_dir)) {
        std::filesystem::create_directories(config_.data_dir);
    }
    
    // 初始化各个模块
    if (!initializeStorage()) {
        LOG_ERROR("存储初始化失败");
        return false;
    }

    ThreadLocalBufferPool::instance().setMaxCachedBlocksPerSize(
        static_cast<size_t>(std::max(1, config_.memory_pool_max_cached_blocks_per_size)));
    
    if (!initializeNetwork()) {
        LOG_ERROR("网络初始化失败");
        return false;
    }
    
    // 设置连接回调
    setupConnectionCallbacks();
    
    // 启动服务器
    running_.store(true);
    stopping_.store(false);
    
    // 启动周期统计线程（可配置）
    if (config_.enable_periodic_stats_log) {
        stats_report_running_.store(true);
        stats_report_thread_ = std::thread(&Server::statsReportThread, this);
    }

    ttl_cleanup_running_.store(true);
    ttl_cleanup_thread_ = std::thread(&Server::ttlCleanupThread, this);

    if (config_.enable_snapshot && config_.snapshot_interval_seconds > 0) {
        snapshot_interval_running_.store(true);
        snapshot_interval_thread_ = std::thread(&Server::snapshotIntervalThread, this);
    }
    
    // 启动信号转发线程：从管道读取信号通知，再在普通线程上下文中触发停止
    signal_thread_running_.store(true);
    signal_thread_ = std::thread([this]() {
        uint8_t sig = 0;
        while (signal_thread_running_.load()) {
            ssize_t n = read(pipe_fds[0], &sig, sizeof(sig));
            if (n == static_cast<ssize_t>(sizeof(sig))) {
                if (!signal_thread_running_.load()) {
                    break;
                }
                this->requestStopFromSignal();
            } else if (n < 0 && errno == EINTR) {
                continue;
            }
        }
    });
    
    // 启动主事件循环
    
    while (running_.load() && !stopping_.load()) {
        main_loop_->loop();
        // 检查是否收到停止信号
        if (stopping_.load()) {
            break;
        }
    }
    
    // 主循环退出后：执行唯一的关闭入口（幂等）
    if (stopping_.load()) {
        stop();
    }
    return true;
}

void Server::requestStopFromSignal() {
    stopping_.store(true);
    if (main_loop_) {
        main_loop_->quit();
    }
}

void Server::advanceShutdown(ShutdownPhase target) {
    int cur = shutdown_phase_.load();
    while (cur < static_cast<int>(target)) {
        if (shutdown_phase_.compare_exchange_weak(cur, cur + 1)) {
            cur = cur + 1;
        }
    }
}

void Server::stop() {
    LOG_DEBUG("进入 Server::stop()");
    LOG_INFO("调用 Server::stop()，running={}, stopping={}, phase={}",
             running_.load(), stopping_.load(), shutdown_phase_.load());

    // 幂等：只允许前进，不允许回退
    const int phase = shutdown_phase_.load();
    if (phase >= static_cast<int>(ShutdownPhase::Completed)) {
        LOG_INFO("Server::stop() 已完成（phase=Completed）");
        return;
    }
    
    stopping_.store(true);
    advanceShutdown(ShutdownPhase::Requested);

    LOG_INFO("正在停止 SunKV Server...");

    // Phase: ThreadsStopped（停止后台线程，避免并发干扰关闭阶段机）
    if (shutdown_phase_.load() < static_cast<int>(ShutdownPhase::ThreadsStopped)) {
        // 停止信号转发线程并唤醒阻塞中的 read
        if (signal_thread_running_.exchange(false)) {
            uint8_t wake = 0;
            (void)!write(pipe_fds[1], &wake, sizeof(wake));
        }
        if (signal_thread_.joinable()) {
            signal_thread_.join();
        }
    
    // 停止 TTL 清理线程
    ttl_cleanup_running_.store(false);
    if (ttl_cleanup_thread_.joinable()) {
        ttl_cleanup_thread_.join();
    }

        snapshot_interval_running_.store(false);
        if (snapshot_interval_thread_.joinable()) {
            snapshot_interval_thread_.join();
        }
    
        // 停止周期统计线程
        stats_report_running_.store(false);
        if (stats_report_thread_.joinable()) {
            stats_report_thread_.join();
        }

        advanceShutdown(ShutdownPhase::ThreadsStopped);
    }

    // Phase: GracefulDone（核心资源关闭）
    if (shutdown_phase_.load() < static_cast<int>(ShutdownPhase::GracefulDone)) {
    gracefulShutdown();
        advanceShutdown(ShutdownPhase::GracefulDone);
    }
    
    running_.store(false);
    advanceShutdown(ShutdownPhase::Completed);
    LOG_INFO("SunKV Server 已停止");
}

void Server::waitForStop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Server::ServerStats Server::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);
    
    return {
        total_connections_.load(),
        current_connections_.load(),
        total_commands_.load(),
        total_operations_.load(),
        static_cast<uint64_t>(uptime.count())
    };
}

bool Server::initializeNetwork() {
    try {
        // 创建主事件循环
        main_loop_ = std::make_unique<EventLoop>();
        
        // 创建 TCP 服务器
        tcp_server_ = std::make_unique<TcpServer>(
            main_loop_.get(),
            "SunKV",
            config_.host,
            config_.port
        );
        
        // 线程池统一由 TcpServer 内部管理，这里仅透传线程数配置
        tcp_server_->setThreadNum(config_.thread_pool_size);
        tcp_server_->setMaxConnections(config_.max_connections);
        TcpSocketTuningOptions sock_tune;
        sock_tune.send_buffer_size = config_.tcp_send_buffer_size;
        sock_tune.recv_buffer_size = config_.tcp_recv_buffer_size;
        sock_tune.tcp_keepalive_idle_seconds = config_.tcp_keepalive_seconds;
        tcp_server_->setConnectionSocketTuning(sock_tune);
        
        LOG_INFO("连接配置: max_connections={}, thread_pool_size={}", config_.max_connections, config_.thread_pool_size);
        LOG_INFO("网络初始化成功");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("网络初始化失败: {}", e.what());
        return false;
    }
}

bool Server::initializeStorage() {
    try {
        // v2：使用 storage2::Factory 组装（Engine + 可选持久化 + 可选装饰器）
        sunkv::storage2::Storage2WiringOptions opt;

        opt.max_storage_bytes = config_.max_memory_mb <= 0
                                    ? 0u
                                    : static_cast<size_t>(config_.max_memory_mb) * 1024u * 1024u;

        // persistence
        opt.enable_wal = config_.enable_wal;
        opt.snapshot_path = config_.snapshot_dir + "/snapshot2.bin";
        opt.wal_path = config_.wal_dir + "/wal2.bin";
        opt.wal_flush_policy = parseWalFlushPolicyString(config_.wal_flush_policy);
        opt.wal_flush_interval_ms = config_.wal_sync_interval_ms;
        opt.wal_async = config_.wal_async;
        opt.wal_max_queue = config_.wal_max_queue <= 0 ? 1u : static_cast<size_t>(config_.wal_max_queue);
        opt.wal_group_commit_linger_ms = config_.wal_group_commit_linger_ms;
        opt.wal_group_commit_max_mutations =
            static_cast<size_t>(std::max(1, config_.wal_group_commit_max_mutations));
        opt.wal_group_commit_max_bytes = static_cast<size_t>(std::max(1, config_.wal_group_commit_max_bytes));
        opt.max_wal_file_size_mb = config_.max_wal_file_size_mb;

        // 确保存储目录存在
        std::filesystem::create_directories(config_.wal_dir);
        std::filesystem::create_directories(config_.snapshot_dir);

        storage2_ = sunkv::storage2::createStorage2(opt);
        if (!storage2_.api || !storage2_.engine) {
            LOG_ERROR("storage2 组装失败：api/engine 为空");
            return false;
        }
        
        // 恢复：先 snapshot 再 WAL
        if (storage2_.orchestrator) {
            if (!storage2_.orchestrator->recoverInto(*storage2_.engine)) {
                LOG_ERROR("storage2 恢复失败");
            return false;
        }
        }

        LOG_INFO("存储初始化成功");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("存储初始化失败: {}", e.what());
        return false;
    }
}

bool Server::create_multi_type_snapshot() {
    try {
        if (!storage2_.engine) return false;
        const std::string snapshot_path = config_.snapshot_dir + "/snapshot2.bin";
        auto records = storage2_.engine->dumpAllLiveRecords();
        return sunkv::storage2::SnapshotWriter::writeToFile(records, snapshot_path);
    } catch (const std::exception& e) {
        LOG_ERROR("创建 storage2 快照失败: {}", e.what());
        return false;
    }
}

// 设置连接回调
void Server::setupConnectionCallbacks() {
    if (!tcp_server_) {
        return;
    }
    
    // 设置连接回调
    tcp_server_->setConnectionCallback([this](const std::shared_ptr<TcpConnection>& conn) {
        onConnection(conn);
    });
    
    // 设置消息回调
    tcp_server_->setMessageCallback([this](const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
        onMessage(conn, data, len);
    });
    
    // 设置写入完成回调
    tcp_server_->setWriteCompleteCallback([](const std::shared_ptr<TcpConnection>& /*conn*/) {
        // 可以在这里实现流量控制等
    });
    
    // 启动 TCP 服务器
    tcp_server_->start();
    
    LOG_INFO("连接回调设置成功");
}

void Server::onConnection(const std::shared_ptr<TcpConnection>& conn) {
    if (conn->connected()) {
    total_connections_.fetch_add(1);
    current_connections_.fetch_add(1);
        LOG_DEBUG("收到新连接: {}", conn->peerAddress());
    } else {
        // TcpConnection 在关闭阶段也会触发 connectionCallback，这里按状态做对称计数
        uint64_t curr = current_connections_.load();
        if (curr > 0) {
            current_connections_.fetch_sub(1);
        }
        LOG_DEBUG("连接已关闭: {}", conn->peerAddress());

        // 清理该连接的输入残留缓冲，避免长时间运行导致 map 增长
        {
            std::lock_guard<std::mutex> lk{conn_inbuf_mu_};
            conn_inbuf_.erase(conn->name());
        }
    }
    
    updateStats();
}

void Server::onMessage(const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
    try {
        // 说明：
        // - Buffer::retrieveAsString(len) 会“消费”输入缓冲；
        // - RESP 命令可能跨多次 read 才完整（pipeline/大包/半包）；
        // - 解析器按连接复用，避免每条命令都构造 RESPParser。
        Buffer* buffer = static_cast<Buffer*>(data);
        std::string chunk = buffer->retrieveAsString(len);
        // 解析器
        std::shared_ptr<ConnParseState> ctx;
        {
            // 锁住连接的输入缓冲
            std::lock_guard<std::mutex> lk{conn_inbuf_mu_};
            // 获取连接的输入缓冲
            auto& slot = conn_inbuf_[conn->name()];
            // 如果连接的输入缓冲不存在，则创建一个
            if (!slot) {
                // 创建一个连接的输入缓冲
                slot = std::make_shared<ConnParseState>();
            }
            // 设置连接的输入缓冲
            ctx = slot;
        }
        // 将数据添加到连接的输入缓冲
        ctx->pending_input.append(chunk);

        // 防御：避免恶意/异常客户端持续喂垃圾导致内存膨胀
        const size_t kMaxConnInbufBytes =
            static_cast<size_t>(std::max(1, config_.max_conn_input_buffer_mb)) * 1024u * 1024u;
        if (ctx->pending_input.size() > kMaxConnInbufBytes) {
            auto error_resp = RESPSerializer::serializeError("Input buffer overflow");
            conn->send(error_resp.data(), error_resp.size());
            conn->forceClose();
            return;
        }

        // 增量解析：pending_input 可能包含多条命令，也可能尾部是不完整命令。
        while (!ctx->pending_input.empty()) {
            const size_t before = ctx->parser.getProcessedBytes();
            auto result = ctx->parser.parse(ctx->pending_input);

            if (!result.success) {
                // RESP 解析错误
                auto error_resp = RESPSerializer::serializeError("Invalid RESP format: " + result.error);
                conn->send(error_resp.data(), error_resp.size());
                return;
            }

            // 解析器返回的是“自本条命令开始以来累计消费字节”，需要转换为本次 parse() 新消费的增量。
            const size_t after = result.processed_bytes;
            const size_t consumed = after >= before ? (after - before) : 0;
            if (consumed > 0) {
                ctx->pending_input.erase(0, consumed);
            }

            if (result.complete && result.value) {
                processCommand(conn, result.value);

                total_commands_.fetch_add(1);
                total_operations_.fetch_add(1);

                // 单条命令完成后重置解析器，继续消费 pending_input 中的剩余命令。
                ctx->parser.reset();
                continue;
            }

            if (!result.complete) {
                // 数据不足，等待下一次网络读取。
                break;
            }

            // 防御：避免异常解析结果造成死循环。
            if (consumed == 0) {
                auto error_resp = RESPSerializer::serializeError("Invalid RESP format: parser made no progress");
                conn->send(error_resp.data(), error_resp.size());
                return;
            }
        }

        // 二次防御：防止异常路径把 pending 长期留大。
        if (ctx->pending_input.size() > kMaxConnInbufBytes) {
            auto error_resp = RESPSerializer::serializeError("Input buffer overflow");
            conn->send(error_resp.data(), error_resp.size());
            conn->forceClose();
            return;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("处理来自 {} 的消息时出错: {}", conn->peerAddress(), e.what());
        auto error = RESPSerializer::serializeError("Internal server error");
        conn->send(error.data(), error.size());
    }
    
    updateStats();
}

void Server::onDisconnection(const std::shared_ptr<TcpConnection>& conn) {
    (void)conn;
    // 当前连接统计已在 onConnection(conn->connected()==false) 路径中处理
    updateStats();
}

bool Server::dispatchArrayCommand_(const std::shared_ptr<TcpConnection>& conn,
                                  const std::string& cmd_name,
                                  const std::vector<RESPValue::Ptr>& cmd_array) {
    return dispatchArrayCommandsLookup(*this, conn, cmd_name, cmd_array);
}

void Server::processCommand(const std::shared_ptr<TcpConnection>& conn, 
                        const RESPValue::Ptr& command) {
    if (!command) {
        auto error = RESPSerializer::serializeError("Invalid command");
        conn->send(error.data(), error.size());
        return;
    }
    
    try {
        
        // 处理数组命令 (如 SET key value, GET key)
        if (command->getType() == RESPType::ARRAY) {
            // 获取命令的数组值
            auto* array_value = static_cast<RESPArray*>(command.get());
            if (array_value && array_value->size() > 0) {
                auto& cmd_array = array_value->getValues();
                // 如果命令的数组值不为空，则获取命令的第一个值
                if (cmd_array[0] && cmd_array[0]->getType() == RESPType::BULK_STRING) {
                    // 获取命令的第一个值
                    auto* bulk_string = static_cast<RESPBulkString*>(cmd_array[0].get());
                    std::string cmd_name = bulk_string->getValue();
                    // 将命令的名称转换为大写
                    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    // 分发命令
                    if (dispatchArrayCommand_(conn, cmd_name, cmd_array)) {
                        return;
                    }
                }
            }
        }
        
        // 处理简单字符串命令
        if (command->getType() == RESPType::SIMPLE_STRING) {
            auto* simple_string = static_cast<RESPSimpleString*>(command.get());
            if (simple_string) {
                std::string cmd = simple_string->toString();
                
                if (cmd == "PING") {
                    conn->send(RESPSerializer::kSimpleStringPong.data(), RESPSerializer::kSimpleStringPong.size());
                    return;
                }
            }
        }
        
        // 如果不支持的命令，返回更兼容 Redis 的错误格式
        auto error = RESPSerializer::serializeError("ERR unknown command");
        conn->send(error.data(), error.size());
        
    } catch (const std::exception& e) {
        LOG_ERROR("执行命令出错: {}", e.what());
        auto error = RESPSerializer::serializeError("Command execution failed");
        conn->send(error.data(), error.size());
    }
}

void Server::snapshotIntervalThread() {
    while (snapshot_interval_running_.load()) {
        const int sec = std::max(1, config_.snapshot_interval_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(sec));
        if (!snapshot_interval_running_.load()) {
            break;
        }
        if (!config_.enable_snapshot) {
            continue;
        }
        if (!create_multi_type_snapshot()) {
            LOG_WARN("周期性快照写入失败（将继续重试）");
        }
    }
}

void Server::ttlCleanupThread() {
    
    while (ttl_cleanup_running_) {
        try {
            cleanupExpiredKeys();
        } catch (const std::exception& e) {
            LOG_ERROR("TTL 清理线程异常: {}", e.what());
        }
        
        const int interval = std::max(1, config_.ttl_cleanup_interval_seconds);
        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }
    
}

void Server::cleanupExpiredKeys() {
    // v2：过期语义主要依赖惰性删除；这里保留后台线程，但只做轻量触发式清理（遍历 keys 并触发一次 pttl）。
    if (!storage2_.api) return;
    auto ks = storage2_.api->keys();
    if (ks.status != sunkv::storage2::StatusCode::Ok) return;
    for (const auto& k : ks.value) {
        auto t = storage2_.api->pttl(k);
        if (t.status == sunkv::storage2::StatusCode::Ok && t.value == -2) {
            expired_keys_cleaned_.fetch_add(1);
        }
    }
}

std::string Server::buildStatsReport() {
    std::ostringstream oss;
    auto stats = getStats();
    size_t kv_size = 0;
    if (storage2_.api) {
        auto r = storage2_.api->dbsize();
        if (r.status == sunkv::storage2::StatusCode::Ok) kv_size = static_cast<size_t>(r.value);
    }

    auto pool_stats = ThreadLocalBufferPool::instance().getStats();
    oss << "uptime_seconds=" << stats.uptime_seconds << "\n"
        << "total_connections=" << stats.total_connections << "\n"
        << "current_connections=" << stats.current_connections << "\n"
        << "total_commands=" << stats.total_commands << "\n"
        << "total_operations=" << stats.total_operations << "\n"
        << "kv_size=" << kv_size << "\n"
        << "expired_keys_cleaned=" << expired_keys_cleaned_.load() << "\n"
        << "memory_pool.hit=" << pool_stats.hit_count << "\n"
        << "memory_pool.miss=" << pool_stats.miss_count << "\n"
        << "memory_pool.release=" << pool_stats.release_count << "\n"
        << "memory_pool.discard=" << pool_stats.discard_count << "\n"
        << "memory_pool.cached_blocks=" << pool_stats.cached_block_count;
    return oss.str();
}

void Server::statsReportThread() {
    const int interval = std::max(1, config_.stats_log_interval_seconds);
    while (stats_report_running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        if (!stats_report_running_.load()) {
            break;
        }
        LOG_INFO("运行统计:\n{}", buildStatsReport());
    }
}

void Server::gracefulShutdown() {
    LOG_INFO("开始执行优雅关闭...");
    LOG_DEBUG("gracefulShutdown() 已启动");
    
    // 1. 停止接受新连接
    if (tcp_server_) {
        LOG_INFO("正在停止 TCP 服务器...");
        LOG_DEBUG("正在停止 TCP 服务器...");
        tcp_server_->stop();
        LOG_DEBUG("TCP 服务器已停止");
    }
    
    // 2. 停止主事件循环
    if (main_loop_) {
        LOG_INFO("正在停止主事件循环...");
        LOG_DEBUG("正在停止主事件循环...");
        main_loop_->quit();
        LOG_DEBUG("主事件循环已停止");
    }
    
    // 3. 等待所有连接关闭（最多等待30秒）
    int wait_count = 0;
    const int max_wait_count = 300; // 30秒，每次100ms
    LOG_DEBUG("等待连接关闭, current={}", current_connections_.load());
    while (current_connections_.load() > 0 && wait_count < max_wait_count) {
        if (wait_count % 10 == 0) {
            LOG_INFO("等待 {} 个连接关闭... ({}/30s)", 
                 current_connections_.load(), wait_count / 10);
        }
        LOG_DEBUG("继续等待, connections={}, count={}",
                  current_connections_.load(), wait_count);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }
    
    LOG_DEBUG("连接等待结束");
    
    if (current_connections_.load() > 0) {
        LOG_WARN("强制关闭剩余 {} 个连接", current_connections_.load());
        LOG_DEBUG("正在强制关闭剩余连接");
    }
    
    // 4. 停止 TTL 清理线程
    if (ttl_cleanup_thread_.joinable()) {
        LOG_INFO("正在停止 TTL 清理线程...");
        LOG_DEBUG("正在停止 TTL 清理线程...");
        ttl_cleanup_running_.store(false);
        ttl_cleanup_thread_.join();
        LOG_DEBUG("TTL 清理线程已停止");
    }
    
    // 5. 创建最终快照（storage2）
    if (storage2_.engine) {
        LOG_INFO("正在创建最终快照...");
        LOG_DEBUG("正在创建最终快照...");
        if (create_multi_type_snapshot()) {
            LOG_INFO("最终快照创建成功");
            LOG_DEBUG("最终快照创建成功");
        } else {
            LOG_WARN("最终快照创建失败");
            LOG_DEBUG("最终快照创建失败");
        }
    }
    
    // 6. 同步并关闭 WAL（storage2 orchestrator）
    if (storage2_.orchestrator) {
        LOG_INFO("正在同步 WAL...");
        LOG_DEBUG("正在同步 WAL...");
        storage2_.orchestrator->flush();
        LOG_INFO("WAL 同步完成");
        LOG_DEBUG("WAL 同步完成");
    }
    
    // 7. 清理存储对象：交由析构完成（storage2 目前无显式 cleanup）
    
    LOG_INFO("优雅关闭完成");
    LOG_DEBUG("gracefulShutdown() 完成");
}

void Server::updateStats() {
    // 这里可以定期更新统计信息
    // 比如计算 QPS、内存使用等
}
