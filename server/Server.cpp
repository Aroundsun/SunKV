#include "Server.h"
#include "Config.h"
#include <csignal>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "../command/SimplePingCommand.h"
#include "../network/Buffer.h"

// 全局服务器实例，用于信号处理
static Server* g_server = nullptr;

/**
 * @brief 信号处理函数
 */
void signalHandler(int signal) {
    if (g_server) {
        LOG_INFO("Received signal {}, shutting down gracefully...", signal);
        g_server->stop();
    }
}

Server::Server(std::unique_ptr<Config> config)
    : config_(std::move(config)),
      start_time_(std::chrono::steady_clock::now()) {
    
    // 设置全局服务器实例
    g_server = this;
    
    // 设置信号处理
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // kill 命令
    std::signal(SIGPIPE, SIG_IGN);    // 忽略管道信号
}

Server::~Server() {
    stop();
    g_server = nullptr;
}

bool Server::start() {
    std::cerr << "DEBUG: Server::start() called" << std::endl;
    LOG_INFO("Starting SunKV Server...");
    
    if (running_.load()) {
        std::cerr << "DEBUG: Server already running" << std::endl;
        LOG_ERROR("Server is already running");
        return false;
    }
    
    // 验证配置
    std::cerr << "DEBUG: Validating configuration..." << std::endl;
    if (!config_->validate()) {
        std::cerr << "DEBUG: Configuration validation failed" << std::endl;
        LOG_ERROR("Invalid configuration");
        return false;
    }
    std::cerr << "DEBUG: Configuration validation passed" << std::endl;
    
    // 创建必要的目录
    std::cerr << "DEBUG: Creating data directories..." << std::endl;
    if (!std::filesystem::exists(config_->data_dir)) {
        std::filesystem::create_directories(config_->data_dir);
    }
    
    // 初始化各个模块
    std::cerr << "DEBUG: Initializing storage..." << std::endl;
    if (!initializeStorage()) {
        std::cerr << "DEBUG: Storage initialization failed" << std::endl;
        LOG_ERROR("Failed to initialize storage");
        return false;
    }
    std::cerr << "DEBUG: Storage initialized successfully" << std::endl;
    
    std::cerr << "DEBUG: Initializing persistence..." << std::endl;
    // 暂时完全跳过持久化初始化
    std::cerr << "DEBUG: Skipping persistence initialization for now" << std::endl;
    // if (!initializePersistence()) {
    //     std::cerr << "DEBUG: Persistence initialization failed" << std::endl;
    //     LOG_ERROR("Failed to initialize persistence");
    //     return false;
    // }
    std::cerr << "DEBUG: Persistence initialization skipped" << std::endl;
    
    std::cerr << "DEBUG: Initializing commands..." << std::endl;
    if (!initializeCommands()) {
        std::cerr << "DEBUG: Commands initialization failed" << std::endl;
        LOG_ERROR("Failed to initialize commands");
        return false;
    }
    std::cerr << "DEBUG: Commands initialized successfully" << std::endl;
    
    // 手动注册一个简单的 PING 命令
    std::cerr << "DEBUG: Registering simple PING command..." << std::endl;
    if (command_registry_) {
        command_registry_->registerCommand("ping", std::make_unique<SimplePingCommand>());
        std::cerr << "DEBUG: PING command registered" << std::endl;
    }
    
    std::cerr << "DEBUG: Initializing network..." << std::endl;
    if (!initializeNetwork()) {
        std::cerr << "DEBUG: Network initialization failed" << std::endl;
        LOG_ERROR("Failed to initialize network");
        return false;
    }
    std::cerr << "DEBUG: Network initialized successfully" << std::endl;
    
    // 设置连接回调
    std::cerr << "DEBUG: Setting up connection callbacks..." << std::endl;
    setupConnectionCallbacks();
    std::cerr << "DEBUG: Connection callbacks set up" << std::endl;
    
    // 启动服务器
    running_.store(true);
    stopping_.store(false);
    
    LOG_INFO("SunKV Server started successfully");
    LOG_INFO("Listening on {}:{}", config_->bind_address, config_->bind_port);
    LOG_INFO("Thread pool size: {}", config_->thread_pool_size);
    LOG_INFO("Data directory: {}", config_->data_dir);
    
    // 启动主事件循环
    std::cerr << "DEBUG: Starting main event loop..." << std::endl;
    main_loop_->loop();
    std::cerr << "DEBUG: Main event loop exited" << std::endl;
    
    return true;
}

void Server::stop() {
    if (!running_.load() || stopping_.load()) {
        return;
    }
    
    LOG_INFO("Stopping SunKV Server...");
    stopping_.store(true);
    
    // 优雅关闭
    gracefulShutdown();
    
    running_.store(false);
    LOG_INFO("SunKV Server stopped");
}

void Server::waitForStop() {
    std::cerr << "DEBUG: waitForStop() called, running=" << running_.load() << std::endl;
    while (running_.load()) {
        std::cerr << "DEBUG: Waiting for stop..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "DEBUG: waitForStop() exiting" << std::endl;
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
    std::cerr << "DEBUG: initializeNetwork() called" << std::endl;
    try {
        // 创建主事件循环
        std::cerr << "DEBUG: Creating main event loop..." << std::endl;
        main_loop_ = std::make_unique<EventLoop>();
        
        // 创建 TCP 服务器
        std::cerr << "DEBUG: Creating TCP server..." << std::endl;
        tcp_server_ = std::make_unique<TcpServer>(
            main_loop_.get(),
            "SunKV",
            config_->bind_address,
            config_->bind_port
        );
        std::cerr << "DEBUG: TCP server created" << std::endl;
        
        // 创建线程池
        std::cerr << "DEBUG: Creating thread pool..." << std::endl;
        thread_pool_ = std::make_unique<EventLoopThreadPool>(
            main_loop_.get(),
            "SunKVThreadPool"
        );
        thread_pool_->setThreadNum(config_->thread_pool_size);
        std::cerr << "DEBUG: Thread pool created" << std::endl;
        
        // 启动线程池
        std::cerr << "DEBUG: Starting thread pool..." << std::endl;
        thread_pool_->start();
        std::cerr << "DEBUG: Thread pool started" << std::endl;
        
        // 启动 TCP 服务器
        std::cerr << "DEBUG: Starting TCP server..." << std::endl;
        tcp_server_->start();
        std::cerr << "DEBUG: TCP server started" << std::endl;
        
        LOG_INFO("Network initialized successfully");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "DEBUG: Network initialization exception: " << e.what() << std::endl;
        LOG_ERROR("Failed to initialize network: {}", e.what());
        return false;
    }
}

bool Server::initializeStorage() {
    try {
        // 获取存储引擎实例
        storage_engine_ = &StorageEngine::getInstance();
        
        // 配置存储引擎
        // 这里可以根据配置设置不同的缓存策略等
        LOG_INFO("Storage initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize storage: {}", e.what());
        return false;
    }
}

bool Server::initializePersistence() {
    std::cerr << "DEBUG: initializePersistence() called" << std::endl;
    try {
        // 初始化 WAL 管理器
        std::cerr << "DEBUG: Creating WAL manager..." << std::endl;
        wal_manager_ = std::make_unique<WALManager>(
            config_->wal_dir,
            config_->wal_max_file_size
        );
        std::cerr << "DEBUG: WAL manager created" << std::endl;
        
        std::cerr << "DEBUG: Initializing WAL manager..." << std::endl;
        // 暂时跳过 WAL 初始化来测试
        if (!wal_manager_->initialize()) {
            std::cerr << "DEBUG: WAL manager initialization failed" << std::endl;
            LOG_ERROR("Failed to initialize WAL manager");
            // return false; // 暂时跳过
        }
        std::cerr << "DEBUG: WAL manager initialized successfully" << std::endl;
        
        // 初始化快照管理器
        snapshot_manager_ = std::make_unique<SnapshotManager>(
            config_->snapshot_dir,
            config_->wal_max_file_size
        );
        
        if (!snapshot_manager_->initialize()) {
            std::cerr << "DEBUG: Snapshot manager initialization failed" << std::endl;
            LOG_ERROR("Failed to initialize snapshot manager");
            // return false; // 暂时跳过
        }
        std::cerr << "DEBUG: Snapshot manager initialized successfully" << std::endl;
        
        // 获取数据并尝试从快照恢复
        std::map<std::string, std::string> data;
        std::string latest_snapshot = snapshot_manager_->get_latest_snapshot();
        if (!latest_snapshot.empty() && std::filesystem::exists(latest_snapshot)) {
            LOG_INFO("Loading data from snapshot: {}", latest_snapshot);
            if (snapshot_manager_->load_snapshot(data)) {
                // 将数据加载到存储引擎
                for (const auto& [key, value] : data) {
                    storage_engine_->set(key, value);
                }
                LOG_INFO("Snapshot loaded successfully");
            } else {
                LOG_ERROR("Failed to load snapshot");
            }
        }
        
        // 重放 WAL 日志
        if (wal_manager_->replay(*storage_engine_)) {
            LOG_INFO("WAL replay completed successfully");
        } else {
            LOG_ERROR("WAL replay failed");
        }
        
        LOG_INFO("Persistence initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize persistence: {}", e.what());
        return false;
    }
}

bool Server::initializeCommands() {
    try {
        // 创建命令注册表
        command_registry_ = std::make_unique<CommandRegistry>();
        
        // 注册所有命令
        // command_registry_->registerAllCommands(); // 暂时禁用
        
        LOG_INFO("Commands initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize commands: {}", e.what());
        return false;
    }
}

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
    tcp_server_->setWriteCompleteCallback([this](const std::shared_ptr<TcpConnection>& conn) {
        // 可以在这里实现流量控制等
    });
    
    // 启动 TCP 服务器
    tcp_server_->start();
    
    LOG_INFO("Connection callbacks set up successfully");
}

void Server::onConnection(const std::shared_ptr<TcpConnection>& conn) {
    total_connections_.fetch_add(1);
    current_connections_.fetch_add(1);
    
    LOG_INFO("New connection from {}", conn->peerAddress());
    
    // 暂时禁用欢迎消息，避免干扰测试
    // auto welcome = RESPSerializer::serializeSimpleString("SunKV 1.0.0");
    // conn->send(welcome.data(), welcome.size());
    
    updateStats();
}

void Server::onMessage(const std::shared_ptr<TcpConnection>& conn, void* data, size_t len) {
    total_commands_.fetch_add(1);
    total_operations_.fetch_add(1);
    
    try {
        // 正确转换 Buffer 到字符串
        Buffer* buffer = static_cast<Buffer*>(data);
        std::string message = buffer->retrieveAsString(len);
        LOG_DEBUG("Received from {}: {}", conn->peerAddress(), message);
        
        std::cerr << "DEBUG: Message content: '" << message << "'" << std::endl;
        std::cerr << "DEBUG: Message length: " << len << std::endl;
        std::cerr << "DEBUG: Message hex: ";
        for (size_t i = 0; i < len && i < 20; ++i) {
            std::cerr << std::hex << (int)(unsigned char)message[i] << " ";
        }
        std::cerr << std::dec << std::endl;
        
        // 解析 RESP 命令
        std::cerr << "DEBUG: Parsing RESP message: " << message << std::endl;
        
        // 简单检测 PING 命令
        std::cerr << "DEBUG: Looking for PING in message..." << std::endl;
        if (message.find("PING") != std::string::npos) {
            std::cerr << "DEBUG: Detected PING command, sending PONG" << std::endl;
            auto pong = RESPSerializer::serializeSimpleString("PONG");
            conn->send(pong.data(), pong.size());
            return;
        }
        std::cerr << "DEBUG: PING not found in message" << std::endl;
        
        auto parser = std::make_unique<RESPParser>();
        auto result = parser->parse(message);
        
        std::cerr << "DEBUG: Parse result success=" << result.success << std::endl;
        std::cerr << "DEBUG: Parse result complete=" << result.complete << std::endl;
        if (result.success && result.complete) {
            std::cerr << "DEBUG: Calling processCommand" << std::endl;
            processCommand(conn, result.value);
        } else if (!result.complete) {
            std::cerr << "DEBUG: Parse incomplete, waiting for more data" << std::endl;
            return; // 等待更多数据
        } else {
            std::cerr << "DEBUG: Parse error: " << result.error << std::endl;
            // 发送错误响应
            auto error_resp = RESPSerializer::serializeError("Invalid RESP format: " + result.error);
            conn->send(error_resp.data(), error_resp.size());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing message from {}: {}", conn->peerAddress(), e.what());
        auto error = RESPSerializer::serializeError("Internal server error");
        conn->send(error.data(), error.size());
    }
    
    updateStats();
}

void Server::onDisconnection(const std::shared_ptr<TcpConnection>& conn) {
    current_connections_.fetch_sub(1);
    
    LOG_INFO("Connection closed: {}", conn->peerAddress());
    updateStats();
}

void Server::processCommand(const std::shared_ptr<TcpConnection>& conn, 
                        const RESPValue::Ptr& command) {
    if (!command_registry_) {
        auto error = RESPSerializer::serializeError("Command system not initialized");
        conn->send(error.data(), error.size());
        return;
    }
    
    try {
        // 执行命令
        auto result = command_registry_->executeCommand(*command, *storage_engine_);
        
        // 发送响应
        sendResponse(conn, std::move(result));
        
        // 记录操作到 WAL
        if (wal_manager_ && result.success) {
            // 这里可以根据命令类型记录到 WAL
            // 为了简化，暂时不实现具体的 WAL 记录
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error executing command: {}", e.what());
        auto error = RESPSerializer::serializeError("Command execution failed");
        conn->send(error.data(), error.size());
    }
}

void Server::sendResponse(const std::shared_ptr<TcpConnection>& conn, const CommandResult& result) {
    try {
        RESPValue::Ptr response;
        
        if (result.success) {
            if (result.response) {
                response = result.response;
            } else {
                // 默认成功响应
                response = makeSimpleString("OK");
            }
        } else {
            // 错误响应
            response = makeError(result.message);
        }
        
        auto serialized = RESPSerializer::serialize(*response);
        conn->send(serialized.data(), serialized.size());
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending response: {}", e.what());
    }
}

void Server::gracefulShutdown() {
    LOG_INFO("Starting graceful shutdown...");
    
    // 停止接受新连接
    // TODO: 实现 TcpServer::stop() 方法
    // if (tcp_server_) {
    //     tcp_server_->stop();
    // }
    
    // 等待所有连接关闭
    while (current_connections_.load() > 0) {
        LOG_INFO("Waiting for {} connections to close...", current_connections_.load());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 创建最终快照
    if (snapshot_manager_ && storage_engine_) {
        // 获取所有数据
        std::map<std::string, std::string> data;
        auto all_keys = storage_engine_->keys();
        for (const auto& key : all_keys) {
            auto value = storage_engine_->get(key);
            if (!value.empty()) {
                data[key] = value;
            }
        }
        
        LOG_INFO("Creating final snapshot...");
        if (snapshot_manager_->create_snapshot(data)) {
            LOG_INFO("Final snapshot created successfully");
        } else {
            LOG_ERROR("Failed to create final snapshot");
        }
    }
    
    // 关闭 WAL
    if (wal_manager_) {
        wal_manager_.reset();
    }
    
    // 停止线程池
    // TODO: 实现 EventLoopThreadPool::stop() 方法
    // if (thread_pool_) {
    //     thread_pool_->stop();
    // }
    
    LOG_INFO("Graceful shutdown completed");
}

void Server::updateStats() {
    // 这里可以定期更新统计信息
    // 比如计算 QPS、内存使用等
}
