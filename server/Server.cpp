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
#include <array>
#include <cctype>
#include <unordered_map>
#include <string_view>
#include "../network/Buffer.h"
#include "../common/MemoryPool.h"
#include "../protocol/RESPSerializer.h"
#include "../protocol/RESPParser.h"
#include "../storage2/Factory.h"
#include "../storage2/persistence/PersistenceOrchestrator.h"
#include "../storage2/persistence/SnapshotWriter.h"

namespace {

auto parseWalFlushPolicyString(const std::string& raw)
    -> sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy {
    std::string policy_string = raw;
    std::transform(policy_string.begin(), policy_string.end(), policy_string.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    while (!policy_string.empty() && (std::isspace(static_cast<unsigned char>(policy_string.front())) != 0)) {
        policy_string.erase(policy_string.begin());
    }
    while (!policy_string.empty() && (std::isspace(static_cast<unsigned char>(policy_string.back())) != 0)) {
        policy_string.pop_back();
    }
    if (policy_string == "never") {
        return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Never;
    }
    if (policy_string == "always") {
        return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Always;
    }
    return sunkv::storage2::PersistenceOrchestrator::Options::WalFlushPolicy::Periodic;
}

} // namespace

// 全局服务器实例，用于信号处理
static Server* g_server = nullptr;
// 管道文件描述符
static std::array<int, 2> pipe_fds = {-1, -1};

/**
 * @brief 信号处理函数
 */
void signalHandler(int signal) {
    if (g_server != nullptr && pipe_fds[1] >= 0) {
        // 信号处理函数中仅做异步信号安全操作：写管道通知
        auto sig = static_cast<uint8_t>(signal);
        const auto bytes_written = write(pipe_fds[1], &sig, sizeof(sig));
        (void)bytes_written;
    }
}

Server::Server(const Config& config)
    : config_(config),
      start_time_(std::chrono::steady_clock::now()) {
    
    // 创建管道用于信号处理
    if (pipe(pipe_fds.data()) == -1) {
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

    // 周期性快照由 PersistenceOrchestrator 内线程负责（见 Factory / initializeStorage）

    // 启动信号转发线程：从管道读取信号通知，再在普通线程上下文中触发停止
    signal_thread_running_.store(true);
    signal_thread_ = std::thread([this]() {
        uint8_t sig = 0;
        while (signal_thread_running_.load()) {
            ssize_t bytes_read = read(pipe_fds[0], &sig, sizeof(sig));
            if (bytes_read == static_cast<ssize_t>(sizeof(sig))) {
                if (!signal_thread_running_.load()) {
                    break;
                }
                this->requestStopFromSignal();
            } else if (bytes_read < 0 && errno == EINTR) {
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
            const auto bytes_written = write(pipe_fds[1], &wake, sizeof(wake));
            (void)bytes_written;
        }
        if (signal_thread_.joinable()) {
            signal_thread_.join();
        }
    
    // 停止 TTL 清理线程
    ttl_cleanup_running_.store(false);
    if (ttl_cleanup_thread_.joinable()) {
        ttl_cleanup_thread_.join();
    }

        if (storage2_.orchestrator) {
            storage2_.orchestrator->stopPeriodicSnapshot();
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
    constexpr int kWaitSleepMs = 100;
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kWaitSleepMs));
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

        constexpr size_t kBytesPerMb = static_cast<size_t>(1024U) * static_cast<size_t>(1024U);
        opt.max_storage_bytes = config_.max_memory_mb <= 0
                                    ? 0U
                                    : static_cast<size_t>(config_.max_memory_mb) * kBytesPerMb;

        // persistence
        opt.enable_wal = config_.enable_wal;
        opt.snapshot_path = config_.snapshot_dir + "/snapshot2.bin";
        opt.wal_path = config_.wal_dir + "/wal2.bin";
        opt.wal_flush_policy = parseWalFlushPolicyString(config_.wal_flush_policy);
        opt.wal_flush_interval_ms = config_.wal_sync_interval_ms;
        opt.wal_async = config_.wal_async;
        opt.wal_max_queue = config_.wal_max_queue <= 0 ? 1U : static_cast<size_t>(config_.wal_max_queue);
        opt.wal_group_commit_linger_ms = config_.wal_group_commit_linger_ms;
        opt.wal_group_commit_max_mutations =
            static_cast<size_t>(std::max(1, config_.wal_group_commit_max_mutations));
        opt.wal_group_commit_max_bytes = static_cast<size_t>(std::max(1, config_.wal_group_commit_max_bytes));
        opt.max_wal_file_size_mb = config_.max_wal_file_size_mb;
        opt.enable_snapshot = config_.enable_snapshot;
        opt.snapshot_interval_seconds = config_.snapshot_interval_seconds;

        // 确保存储目录存在
        std::filesystem::create_directories(config_.wal_dir);
        std::filesystem::create_directories(config_.snapshot_dir);

        storage2_ = sunkv::storage2::createStorage2(opt);
        if (storage2_.api == nullptr || storage2_.engine == nullptr) {
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

bool Server::create_multi_type_snapshot() const {
    try {
        if (storage2_.orchestrator) {
            return storage2_.orchestrator->takeSnapshotNow();
        }
        if (storage2_.engine == nullptr) {
            return false;
        }
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

        // 清理该连接的输入缓冲与订阅关系，避免残留状态。
        clearSubscriptionsForConnection_(conn);
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
            std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
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
        const auto pendingReadableBytes = [&ctx]() -> size_t {
            if (ctx->pending_offset >= ctx->pending_input.size()) {
                return 0;
            }
            return ctx->pending_input.size() - ctx->pending_offset;
        };

        // 防御：避免恶意/异常客户端持续喂垃圾导致内存膨胀
        const size_t kMaxConnInbufBytes =
            static_cast<size_t>(std::max(1, config_.max_conn_input_buffer_mb)) * 1024U * 1024U;
        if (pendingReadableBytes() > kMaxConnInbufBytes) {
            auto error_resp = RESPSerializer::serializeError("Input buffer overflow");
            conn->send(error_resp.data(), error_resp.size());
            conn->forceClose();
            return;
        }

        struct WriteCoalescingScope final {
            explicit WriteCoalescingScope(const std::shared_ptr<TcpConnection>& connection) : conn(connection) {
                conn->beginWriteCoalescing();
            }
            ~WriteCoalescingScope() { conn->endWriteCoalescing(); }
            std::shared_ptr<TcpConnection> conn;
        } write_scope(conn);

        // 增量解析：pending_input 可能包含多条命令，也可能尾部是不完整命令。
        while (ctx->pending_offset < ctx->pending_input.size()) {
            std::string_view unread{ctx->pending_input.data() + ctx->pending_offset,
                                    ctx->pending_input.size() - ctx->pending_offset};
            const size_t before = ctx->parser.getProcessedBytes();
            auto result = ctx->parser.parse(unread);

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
                ctx->pending_offset += consumed;
            }

            if (result.complete && result.value) {
                processCommand(conn, ctx, result.value);

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

        // 仅在必要时做一次压缩，避免每条命令都触发前删搬移。
        if (ctx->pending_offset >= ctx->pending_input.size()) {
            ctx->pending_input.clear();
            ctx->pending_offset = 0;
        } else {
            constexpr size_t kCompactMinBytes = static_cast<size_t>(4) * static_cast<size_t>(1024);
            if (ctx->pending_offset >= kCompactMinBytes &&
                ctx->pending_offset * 2 >= ctx->pending_input.size()) {
                ctx->pending_input.erase(0, ctx->pending_offset);
                ctx->pending_offset = 0;
            }
        }

        // 二次防御：防止异常路径把 pending 长期留大。
        if (pendingReadableBytes() > kMaxConnInbufBytes) {
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
// 判断连接是否处于订阅模式
bool Server::isSubscribeMode_(const std::shared_ptr<TcpConnection>& conn) {
    std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
    auto iter = conn_inbuf_.find(conn->name());
    if (iter == conn_inbuf_.end() || !iter->second) {
        return false;
    }
    return !iter->second->subscribed_channels.empty();
}
// 订阅频道
size_t Server::subscribeChannel_(const std::shared_ptr<TcpConnection>& conn, const std::string& channel) {
    size_t subscribed_count = 0;
    {
        std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
        auto iter = conn_inbuf_.find(conn->name());
        if (iter == conn_inbuf_.end() || !iter->second) {
            return 0;
        }
        iter->second->subscribed_channels.insert(channel);
        subscribed_count = iter->second->subscribed_channels.size();
    }

    {
        std::lock_guard<std::mutex> lock{pubsub_mu_};
        channel_subscribers_[channel].insert(conn);
    }
    return subscribed_count;
}

// 取消订阅频道
size_t Server::unsubscribeChannel_(const std::shared_ptr<TcpConnection>& conn, const std::string& channel) {
    size_t subscribed_count = 0;
    {
        std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
        auto iter = conn_inbuf_.find(conn->name());
        if (iter != conn_inbuf_.end() && iter->second) {
            iter->second->subscribed_channels.erase(channel);
            subscribed_count = iter->second->subscribed_channels.size();
        }
    }

    {
        std::lock_guard<std::mutex> lock{pubsub_mu_};
        auto channel_iter = channel_subscribers_.find(channel);
        if (channel_iter != channel_subscribers_.end()) {
            channel_iter->second.erase(conn);
            if (channel_iter->second.empty()) {
                channel_subscribers_.erase(channel_iter);
            }
        }
    }
    return subscribed_count;
}
// 取消所有订阅频道
std::vector<std::pair<std::string, size_t>> Server::unsubscribeAllChannels_(const std::shared_ptr<TcpConnection>& conn) {
    std::vector<std::string> channels;
    {
        std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
        auto iter = conn_inbuf_.find(conn->name());
        if (iter != conn_inbuf_.end() && iter->second) {
            channels.assign(iter->second->subscribed_channels.begin(), iter->second->subscribed_channels.end());
            iter->second->subscribed_channels.clear();
        }
    }

    {
        std::lock_guard<std::mutex> lock{pubsub_mu_};
        for (const auto& channel : channels) {
            auto channel_iter = channel_subscribers_.find(channel);
            if (channel_iter != channel_subscribers_.end()) {
                channel_iter->second.erase(conn);
                if (channel_iter->second.empty()) {
                    channel_subscribers_.erase(channel_iter);
                }
            }
        }
    }

    std::vector<std::pair<std::string, size_t>> result;
    result.reserve(channels.size());
    for (const auto& channel : channels) {
        result.emplace_back(channel, 0);
    }
    return result;
}
// 发布消息
int64_t Server::publishMessage_(const std::string& channel, const std::string& payload) {
    std::vector<std::shared_ptr<TcpConnection>> subscribers;
    {
        std::lock_guard<std::mutex> lock{pubsub_mu_};
        auto iter = channel_subscribers_.find(channel);
        if (iter == channel_subscribers_.end()) {
            return 0;
        }
        subscribers.reserve(iter->second.size());
        for (const auto& subscriber : iter->second) {
            if (subscriber && subscriber->connected()) {
                subscribers.push_back(subscriber);
            }
        }
    }

    std::string message = "*3\r\n";
    message += RESPSerializer::serializeBulkString("message");
    message += RESPSerializer::serializeBulkString(channel);
    message += RESPSerializer::serializeBulkString(payload);
    for (const auto& subscriber : subscribers) {
        subscriber->send(message.data(), message.size());
    }
    return static_cast<int64_t>(subscribers.size());
}

void Server::clearSubscriptionsForConnection_(const std::shared_ptr<TcpConnection>& conn) {
    std::vector<std::string> channels;
    {
        std::lock_guard<std::mutex> lock{conn_inbuf_mu_};
        auto iter = conn_inbuf_.find(conn->name());
        if (iter == conn_inbuf_.end() || !iter->second) {
            return;
        }
        channels.assign(iter->second->subscribed_channels.begin(), iter->second->subscribed_channels.end());
        conn_inbuf_.erase(iter);
    }

    std::lock_guard<std::mutex> lock{pubsub_mu_};
    for (const auto& channel : channels) {
        auto channel_iter = channel_subscribers_.find(channel);
        if (channel_iter != channel_subscribers_.end()) {
            channel_iter->second.erase(conn);
            if (channel_iter->second.empty()) {
                channel_subscribers_.erase(channel_iter);
            }
        }
    }
}

void Server::processCommand(const std::shared_ptr<TcpConnection>& conn,
                            const std::shared_ptr<Server::ConnParseState>& ctx,
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
            if (array_value != nullptr && array_value->size() > 0) {
                const auto& cmd_array = array_value->getValues();
                // 如果命令的数组值不为空，则获取命令的第一个值
                if (cmd_array[0] && cmd_array[0]->getType() == RESPType::BULK_STRING) {
                    // 获取命令的第一个值
                    auto* bulk_string = static_cast<RESPBulkString*>(cmd_array[0].get());
                    std::string cmd_name = bulk_string->getValue();
                    // 将命令的名称转换为大写
                    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(),
                                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });

                    if (cmd_name == "WATCH" || cmd_name == "UNWATCH") {
                        auto err = RESPSerializer::serializeError("ERR WATCH/UNWATCH is not supported");
                        conn->send(err.data(), err.size());
                        return;
                    }
                    // 订阅态命令白名单准入门禁
                    if (ctx && isSubscribeMode_(conn)) {
                        // 允许的订阅态命令
                        const bool allowed = (cmd_name == "SUBSCRIBE" || cmd_name == "UNSUBSCRIBE" ||
                                              cmd_name == "PING" || cmd_name == "QUIT");
                        if (!allowed) {
                            auto err = RESPSerializer::serializeError(
                                "ERR only SUBSCRIBE/UNSUBSCRIBE/PING/QUIT allowed in this context");
                            conn->send(err.data(), err.size());
                            return;
                        }
                    }

                    if (cmd_name == "QUIT") {
                        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
                        conn->forceClose();
                        return;
                    }

                    // 事务控制命令：MULTI / EXEC / DISCARD
                    if (cmd_name == "MULTI") {
                        if (ctx && ctx->in_multi) {
                            auto err = RESPSerializer::serializeError("ERR MULTI calls can not be nested");
                            conn->send(err.data(), err.size());
                            return;
                        }
                        // 设置事务状态
                        if (ctx) {
                            ctx->in_multi = true;
                            ctx->queued_commands.clear();
                        }
                        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
                        return;
                    }
                    if (cmd_name == "DISCARD") {
                        if (!ctx || !ctx->in_multi) {
                            auto err = RESPSerializer::serializeError("ERR DISCARD without MULTI");
                            conn->send(err.data(), err.size());
                            return;
                        }
                        ctx->in_multi = false;
                        ctx->queued_commands.clear();
                        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
                        return;
                    }
                    if (cmd_name == "EXEC") {
                        if (!ctx || !ctx->in_multi) {
                            auto err = RESPSerializer::serializeError("ERR EXEC without MULTI");
                            conn->send(err.data(), err.size());
                            return;
                        }
                        //拼接响应
                        std::string out = "*" + std::to_string(ctx->queued_commands.size()) + "\r\n";
                        for (const auto& queued_command : ctx->queued_commands) {
                            out += executeArrayCommandToResp(*this, queued_command.cmd_name, queued_command.cmd_array);
                        }
                        ctx->in_multi = false;
                        ctx->queued_commands.clear();
                        conn->send(out.data(), out.size());
                        return;
                    }

                    // 事务态：非控制命令入队并返回 +QUEUED
                    // 入队：不执行任何命令，将命令存入待执行队列中
                    if (ctx && ctx->in_multi) {
                        ConnParseState::QueuedCommand queued_command;
                        queued_command.cmd_name = cmd_name;
                        queued_command.cmd_array = cmd_array;
                        ctx->queued_commands.push_back(std::move(queued_command));
                        static constexpr std::string_view kQueued = "+QUEUED\r\n";
                        conn->send(kQueued.data(), kQueued.size());
                        return;
                    }

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
            if (simple_string != nullptr) {
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
    if (!storage2_.api) {
        return;
    }
    auto keys_result = storage2_.api->keys();
    if (keys_result.status != sunkv::storage2::StatusCode::Ok) {
        return;
    }
    for (const auto& key : keys_result.value) {
        auto ttl_result = storage2_.api->pttl(key);
        if (ttl_result.status == sunkv::storage2::StatusCode::Ok && ttl_result.value == -2) {
            expired_keys_cleaned_.fetch_add(1);
        }
    }
}

std::string Server::buildStatsReport() {
    std::ostringstream oss;
    auto stats = getStats();
    size_t kv_size = 0;
    if (storage2_.api) {
        auto dbsize_result = storage2_.api->dbsize();
        if (dbsize_result.status == sunkv::storage2::StatusCode::Ok) {
            kv_size = static_cast<size_t>(dbsize_result.value);
        }
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
    constexpr int kWaitLogEvery = 10;
    constexpr int kWaitSleepMs = 100;
    int wait_count = 0;
    const int max_wait_count = 300; // 30秒，每次100ms
    LOG_DEBUG("等待连接关闭, current={}", current_connections_.load());
    while (current_connections_.load() > 0 && wait_count < max_wait_count) {
        if (wait_count % kWaitLogEvery == 0) {
            LOG_INFO("等待 {} 个连接关闭... ({}/30s)", 
                 current_connections_.load(), wait_count / kWaitLogEvery);
        }
        LOG_DEBUG("继续等待, connections={}, count={}",
                  current_connections_.load(), wait_count);
        std::this_thread::sleep_for(std::chrono::milliseconds(kWaitSleepMs));
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
    
    // 5. 创建最终快照（storage2）：须先于 WAL flush，恢复顺序为 recoverInto 先读快照再回放 WAL。
    if (storage2_.engine != nullptr) {
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
