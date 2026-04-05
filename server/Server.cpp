#include "Server.h"
#include "../common/Config.h"
#include <csignal>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <filesystem>
#include "../command/SimplePingCommand.h"
#include "../network/Buffer.h"

// 全局服务器实例，用于信号处理
static Server* g_server = nullptr;
static int pipe_fds[2];

/**
 * @brief 信号处理函数
 */
void signalHandler(int signal) {
    if (g_server) {
        write(STDOUT_FILENO, "Received shutdown signal\n", 24);
        write(STDOUT_FILENO, "Calling stop() method\n", 22);
        // 直接在信号处理中设置停止标志
        g_server->setStopping();
        g_server->stopMainLoop();
        write(STDOUT_FILENO, "Signal handler completed\n", 26);
    }
}

Server::Server(const Config& config)
    : config_(config),
      start_time_(std::chrono::steady_clock::now()) {
    
    // 创建管道用于信号处理
    if (pipe(pipe_fds) == -1) {
        perror("pipe");
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
    stop();
    g_server = nullptr;
    
    // 关闭管道
    close(pipe_fds[0]);
    close(pipe_fds[1]);
}

bool Server::start() {
    #ifdef DEBUG
#endif
    LOG_INFO("Starting SunKV Server...");
    
    if (running_.load()) {
        LOG_ERROR("Server is already running");
        return false;
    }
    
    // 验证配置
    if (!config_.validate()) {
        LOG_ERROR("Invalid configuration");
        return false;
    }
    
    // 创建必要的目录
    if (!std::filesystem::exists(config_.data_dir)) {
        std::filesystem::create_directories(config_.data_dir);
    }
    
    // 初始化各个模块
    if (!initializeStorage()) {
        LOG_ERROR("Failed to initialize storage");
        return false;
    }
    
    if (!initializePersistence()) {
        LOG_ERROR("Failed to initialize persistence");
        return false;
    }
    
    if (!initializeCommands()) {
        LOG_ERROR("Failed to initialize commands");
        return false;
    }
    
    // 手动注册一个简单的 PING 命令
    if (command_registry_) {
        command_registry_->registerCommand("ping", std::make_unique<SimplePingCommand>());
    }
    
    if (!initializeNetwork()) {
        LOG_ERROR("Failed to initialize network");
        return false;
    }
    
    // 设置连接回调
    setupConnectionCallbacks();
    
    // 启动服务器
    running_.store(true);
    stopping_.store(false);
    
    // 启动主事件循环
    
    while (running_.load() && !stopping_.load()) {
        main_loop_->loop();
        // 检查是否收到停止信号
        if (stopping_.load()) {
            break;
        }
    }
    
    
    return true;
}

void Server::stop() {
    std::cerr << "DEBUG: Server::stop() entry point" << std::endl;
    LOG_INFO("Server::stop() called, running={}, stopping={}", 
             running_.load(), stopping_.load());
    
    // 检查是否已经执行过优雅关闭
    static std::atomic<bool> shutdown_executed{false};
    if (shutdown_executed.exchange(true)) {
        LOG_INFO("Server::stop() graceful shutdown already executed");
        std::cerr << "DEBUG: Server::stop() graceful shutdown already executed" << std::endl;
        return;
    }
    
    std::cerr << "DEBUG: Server::stop() proceeding with shutdown" << std::endl;
    LOG_INFO("Stopping SunKV Server...");
    stopping_.store(true);
    
    // 停止 TTL 清理线程
    ttl_cleanup_running_.store(false);
    if (ttl_cleanup_thread_.joinable()) {
        ttl_cleanup_thread_.join();
    }
    
    // 优雅关闭
    gracefulShutdown();
    
    running_.store(false);
    LOG_INFO("SunKV Server stopped");
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
        
        // 创建线程池
        thread_pool_ = std::make_unique<EventLoopThreadPool>(
            main_loop_.get(),
            "SunKVThreadPool"
        );
        thread_pool_->setThreadNum(config_.thread_pool_size);
        
        // 启动线程池
        thread_pool_->start();
        
        // 启动 TCP 服务器
        tcp_server_->start();
        
        LOG_INFO("Network initialized successfully");
        return true;
    } catch (const std::exception& e) {
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
    try {
        // 初始化 WAL 管理器
        wal_manager_ = std::make_unique<WALManager>(
            config_.wal_dir,
            config_.max_wal_file_size_mb * 1024 * 1024
        );
        
        if (!wal_manager_->initialize()) {
            LOG_ERROR("Failed to initialize WAL manager");
            return false;
        }
        
        // 初始化快照管理器
        snapshot_manager_ = std::make_unique<SnapshotManager>(
            config_.snapshot_dir
        );
        
        if (!snapshot_manager_->initialize()) {
            LOG_ERROR("Failed to initialize snapshot manager");
            return false;
        }
        
        // 启用快照恢复
        std::map<std::string, DataValue> multi_data;
        std::string latest_snapshot = snapshot_manager_->get_latest_snapshot();
        bool snapshot_loaded = false;
        
        if (!latest_snapshot.empty() && std::filesystem::exists(latest_snapshot)) {
            if (load_multi_type_snapshot(multi_data)) {
                snapshot_loaded = true;
            } else {
            }
        } else {
        }
        
        // 启用 WAL 恢复
        
        if (snapshot_loaded) {
            // 快照恢复成功，跳过 WAL 恢复避免重复操作
            // 直接使用快照数据
            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
            multi_storage_ = multi_data;
        } else {
            // 没有快照，进行 WAL 恢复
            if (wal_manager_->replay_multi_type(multi_data)) {
                std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                multi_storage_ = multi_data;
            } else {
            }
        }
        
        LOG_INFO("Persistence initialized successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize persistence: {}", e.what());
        return false;
    }
}

bool Server::load_multi_type_snapshot(std::map<std::string, DataValue>& data) {
    try {
        // 首先尝试加载现有的字符串快照
        std::map<std::string, std::string> string_data;
        if (snapshot_manager_->load_snapshot(string_data)) {
            // 将字符串数据转换为多数据类型格式
            for (const auto& [key, value] : string_data) {
                DataValue data_value(value);  // 创建字符串类型的 DataValue
                data[key] = data_value;
            }
            LOG_INFO("Converted {} string entries to multi-type format", string_data.size());
            return true;
        }
        
        LOG_WARN("No snapshot data found");
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load multi-type snapshot: {}", e.what());
        return false;
    }
}

bool Server::create_multi_type_snapshot() {
    try {
        std::lock_guard<std::mutex> lock(multi_storage_mutex_);
        if (snapshot_manager_) {
            bool success = snapshot_manager_->create_multi_type_snapshot(multi_storage_);
            if (success) {
            } else {
            }
            return success;
        }
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create multi-type snapshot: {}", e.what());
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
        
        for (size_t i = 0; i < len && i < 20; ++i) {
            std::cerr << std::hex << (int)(unsigned char)message[i] << " ";
        }
        std::cerr << std::dec << std::endl;
        
        // 解析 RESP 命令
        
        // 使用真正的 RESP 解析器
        auto parser = std::make_unique<RESPParser>();
        auto result = parser->parse(message);
        
        
        if (result.success && result.complete && result.value) {
            processCommand(conn, result.value);
            return;
        } else if (!result.complete) {
            return; // 等待更多数据
        } else {
            // 发送错误响应
            auto error_resp = RESPSerializer::serializeError("Invalid RESP format: " + result.error);
            conn->send(error_resp.data(), error_resp.size());
            return;
        }
        
        // 如果 RESP 解析失败，尝试简单的字符串匹配 (备用方案)
        if (message.find("PING") != std::string::npos) {
            auto pong = RESPSerializer::serializeSimpleString("PONG");
            conn->send(pong.data(), pong.size());
            return;
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
    if (!command) {
        auto error = RESPSerializer::serializeError("Invalid command");
        conn->send(error.data(), error.size());
        return;
    }
    
    try {
        
        // 处理数组命令 (如 SET key value, GET key)
        if (command->getType() == RESPType::ARRAY) {
            auto* array_value = static_cast<RESPArray*>(command.get());
            if (array_value && array_value->size() > 0) {
                auto& cmd_array = array_value->getValues();
                if (cmd_array[0] && cmd_array[0]->getType() == RESPType::BULK_STRING) {
                    auto* bulk_string = static_cast<RESPBulkString*>(cmd_array[0].get());
                    std::string cmd_name = bulk_string->getValue();
                    
                    if (cmd_name == "PING") {
                        auto pong = RESPSerializer::serializeSimpleString("PONG");
                        conn->send(pong.data(), pong.size());
                        return;
                    }
                    
                    if (cmd_name == "SNAPSHOT") {
                        if (create_multi_type_snapshot()) {
                            auto ok = RESPSerializer::serializeSimpleString("OK");
                            conn->send(ok.data(), ok.size());
                        } else {
                            auto error = RESPSerializer::serializeError("Snapshot creation failed");
                            conn->send(error.data(), error.size());
                        }
                        return;
                    }
                    
                    if (cmd_name == "SET" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING && 
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            std::string key = key_bulk->getValue();
                            std::string value = value_bulk->getValue();
                            
                            {
                                std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                                multi_storage_[key] = DataValue(value);  // 创建新的字符串值，默认无 TTL
                            }
                            
                            // 记录到 WAL
                            if (wal_manager_) {
                                wal_manager_->write_set(key, value);
                            }
                            
                            auto ok = RESPSerializer::serializeSimpleString("OK");
                            conn->send(ok.data(), ok.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "GET" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::STRING || it->second.isExpired()) {
                                if (it != multi_storage_.end() && it->second.isExpired()) {
                                    multi_storage_.erase(it);  // 删除过期键
                                    expired_keys_cleaned_++;
                                }
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            auto response = RESPSerializer::serializeBulkString(it->second.string_value);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "DEL" && cmd_array.size() >= 2) {
                        int deleted_count = 0;
                        {
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            for (size_t i = 1; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    std::string key = key_bulk->getValue();
                                    auto it = multi_storage_.find(key);
                                    if (it != multi_storage_.end()) {
                                        multi_storage_.erase(it);
                                        deleted_count++;
                                        
                                        // 记录到 WAL
                                        if (wal_manager_) {
                                            wal_manager_->write_del(key);
                                        }
                                    } else {
                                    }
                                }
                            }
                        }
                        auto response = RESPSerializer::serializeInteger(deleted_count);
                        conn->send(response.data(), response.size());
                        return;
                    }
                    
                    if (cmd_name == "EXISTS" && cmd_array.size() >= 2) {
                        int exists_count = 0;
                        {
                            std::lock_guard<std::mutex> lock(simple_storage_mutex_);
                            for (size_t i = 1; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    std::string key = key_bulk->getValue();
                                    if (simple_storage_.find(key) != simple_storage_.end()) {
                                        exists_count++;
                                    }
                                }
                            }
                        }
                        auto response = RESPSerializer::serializeInteger(exists_count);
                        conn->send(response.data(), response.size());
                        return;
                    }
                    
                    if (cmd_name == "KEYS") {
                        std::string keys_array = "*";
                        std::vector<std::string> keys;
                        {
                            std::lock_guard<std::mutex> lock(simple_storage_mutex_);
                            for (const auto& pair : simple_storage_) {
                                keys.push_back(pair.first);
                            }
                        }
                        keys_array += std::to_string(keys.size()) + "\r\n";
                        for (const auto& key : keys) {
                            keys_array += "$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
                        }
                        conn->send(keys_array.data(), keys_array.size());
                        return;
                    }
                    
                    if (cmd_name == "DBSIZE") {
                        int64_t size = 0;
                        {
                            std::lock_guard<std::mutex> lock(simple_storage_mutex_);
                            size = simple_storage_.size();
                        }
                        auto response = RESPSerializer::serializeInteger(size);
                        conn->send(response.data(), response.size());
                        return;
                    }
                    
                    if (cmd_name == "FLUSHALL") {
                        {
                            std::lock_guard<std::mutex> lock(simple_storage_mutex_);
                            simple_storage_.clear();
                        }
                        {
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            multi_storage_.clear();
                        }
                        auto ok = RESPSerializer::serializeSimpleString("OK");
                        conn->send(ok.data(), ok.size());
                        return;
                    }
                    
                    // List 命令
                    if (cmd_name == "LPUSH" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto& data = multi_storage_[key];
                            if (data.type != DataType::LIST && data.type != DataType::STRING) {
                                auto error = RESPSerializer::serializeError("WRONGTYPE Operation against a key holding the wrong kind of value");
                                conn->send(error.data(), error.size());
                                return;
                            }
                            
                            // 如果是字符串类型，转换为列表
                            if (data.type == DataType::STRING) {
                                data.list_value.push_front(data.string_value);
                                data.type = DataType::LIST;
                            }
                            
                            for (size_t i = 2; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    data.list_value.push_front(value_bulk->getValue());
                                }
                            }
                            
                            auto response = RESPSerializer::serializeInteger(data.list_value.size());
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "RPUSH" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto& data = multi_storage_[key];
                            if (data.type != DataType::LIST && data.type != DataType::STRING) {
                                auto error = RESPSerializer::serializeError("WRONGTYPE Operation against a key holding the wrong kind of value");
                                conn->send(error.data(), error.size());
                                return;
                            }
                            
                            // 如果是字符串类型，转换为列表
                            if (data.type == DataType::STRING) {
                                data.list_value.push_back(data.string_value);
                                data.type = DataType::LIST;
                            }
                            
                            for (size_t i = 2; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    data.list_value.push_back(value_bulk->getValue());
                                }
                            }
                            
                            auto response = RESPSerializer::serializeInteger(data.list_value.size());
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "LPOP" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::LIST) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            if (it->second.list_value.empty()) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            std::string value = it->second.list_value.front();
                            it->second.list_value.pop_front();
                            
                            auto response = RESPSerializer::serializeBulkString(value);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "RPOP" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::LIST) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            if (it->second.list_value.empty()) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            std::string value = it->second.list_value.back();
                            it->second.list_value.pop_back();
                            
                            auto response = RESPSerializer::serializeBulkString(value);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "LLEN" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::LIST || it->second.isExpired()) {
                                if (it != multi_storage_.end() && it->second.isExpired()) {
                                    multi_storage_.erase(it);  // 删除过期键
                                    expired_keys_cleaned_++;
                                }
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            auto response = RESPSerializer::serializeInteger(it->second.list_value.size());
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    // Set 命令
                    if (cmd_name == "SADD" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto& data = multi_storage_[key];
                            if (data.type != DataType::SET && data.type != DataType::STRING) {
                                auto error = RESPSerializer::serializeError("WRONGTYPE Operation against a key holding the wrong kind of value");
                                conn->send(error.data(), error.size());
                                return;
                            }
                            
                            // 如果是字符串类型，转换为集合
                            if (data.type == DataType::STRING) {
                                data.set_value.insert(data.string_value);
                                data.type = DataType::SET;
                            }
                            
                            int added_count = 0;
                            for (size_t i = 2; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    if (data.set_value.insert(value_bulk->getValue()).second) {
                                        added_count++;
                                    }
                                }
                            }
                            
                            auto response = RESPSerializer::serializeInteger(added_count);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "SREM" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::SET) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int removed_count = 0;
                            for (size_t i = 2; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    if (it->second.set_value.erase(value_bulk->getValue()) > 0) {
                                        removed_count++;
                                    }
                                }
                            }
                            
                            auto response = RESPSerializer::serializeInteger(removed_count);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "SMEMBERS" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::SET) {
                                std::string empty_array = "*0\r\n";
                                conn->send(empty_array.data(), empty_array.size());
                                return;
                            }
                            
                            std::string members_array = "*" + std::to_string(it->second.set_value.size()) + "\r\n";
                            for (const auto& member : it->second.set_value) {
                                members_array += "$" + std::to_string(member.length()) + "\r\n" + member + "\r\n";
                            }
                            conn->send(members_array.data(), members_array.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "SCARD" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::SET) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            auto response = RESPSerializer::serializeInteger(it->second.set_value.size());
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "SISMEMBER" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING &&
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* member_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            std::string key = key_bulk->getValue();
                            std::string member = member_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::SET) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int is_member = it->second.set_value.count(member) > 0 ? 1 : 0;
                            auto response = RESPSerializer::serializeInteger(is_member);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    // Hash 命令
                    if (cmd_name == "HSET" && cmd_array.size() >= 4) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING &&
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING &&
                            cmd_array[3] && cmd_array[3]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[3].get());
                            std::string key = key_bulk->getValue();
                            std::string field = field_bulk->getValue();
                            std::string value = value_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto& data = multi_storage_[key];
                            if (data.type != DataType::HASH && data.type != DataType::STRING) {
                                auto error = RESPSerializer::serializeError("WRONGTYPE Operation against a key holding the wrong kind of value");
                                conn->send(error.data(), error.size());
                                return;
                            }
                            
                            // 如果是字符串类型，转换为哈希
                            if (data.type == DataType::STRING) {
                                data.hash_value["value"] = data.string_value;
                                data.type = DataType::HASH;
                            }
                            
                            bool is_new_field = data.hash_value.find(field) == data.hash_value.end();
                            data.hash_value[field] = value;
                            
                            auto response = RESPSerializer::serializeInteger(is_new_field ? 1 : 0);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "HGET" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING &&
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            std::string key = key_bulk->getValue();
                            std::string field = field_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::HASH) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            auto field_it = it->second.hash_value.find(field);
                            if (field_it == it->second.hash_value.end()) {
                                auto nil = RESPSerializer::serializeNullBulkString();
                                conn->send(nil.data(), nil.size());
                                return;
                            }
                            
                            auto response = RESPSerializer::serializeBulkString(field_it->second);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "HDEL" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::HASH) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int deleted_count = 0;
                            for (size_t i = 2; i < cmd_array.size(); ++i) {
                                if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
                                    auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
                                    if (it->second.hash_value.erase(field_bulk->getValue()) > 0) {
                                        deleted_count++;
                                    }
                                }
                            }
                            
                            auto response = RESPSerializer::serializeInteger(deleted_count);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "HGETALL" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::HASH) {
                                std::string empty_array = "*0\r\n";
                                conn->send(empty_array.data(), empty_array.size());
                                return;
                            }
                            
                            std::string hash_array = "*" + std::to_string(it->second.hash_value.size() * 2) + "\r\n";
                            for (const auto& pair : it->second.hash_value) {
                                hash_array += "$" + std::to_string(pair.first.length()) + "\r\n" + pair.first + "\r\n";
                                hash_array += "$" + std::to_string(pair.second.length()) + "\r\n" + pair.second + "\r\n";
                            }
                            conn->send(hash_array.data(), hash_array.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "HLEN" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::HASH) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            auto response = RESPSerializer::serializeInteger(it->second.hash_value.size());
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "HEXISTS" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING &&
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            std::string key = key_bulk->getValue();
                            std::string field = field_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end() || it->second.type != DataType::HASH) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int exists = it->second.hash_value.count(field) > 0 ? 1 : 0;
                            auto response = RESPSerializer::serializeInteger(exists);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    // TTL 命令
                    if (cmd_name == "EXPIRE" && cmd_array.size() >= 3) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING &&
                            cmd_array[2] && cmd_array[2]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            auto* ttl_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
                            std::string key = key_bulk->getValue();
                            
                            try {
                                int64_t ttl_seconds = std::stoll(ttl_bulk->getValue());
                                
                                std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                                auto it = multi_storage_.find(key);
                                if (it == multi_storage_.end()) {
                                    auto response = RESPSerializer::serializeInteger(0);
                                    conn->send(response.data(), response.size());
                                    return;
                                }
                                
                                it->second.setTTL(ttl_seconds);
                                auto response = RESPSerializer::serializeInteger(1);
                                conn->send(response.data(), response.size());
                                return;
                            } catch (const std::exception& e) {
                                auto error = RESPSerializer::serializeError("Invalid TTL value");
                                conn->send(error.data(), error.size());
                                return;
                            }
                        }
                    }
                    
                    if (cmd_name == "TTL" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end()) {
                                auto response = RESPSerializer::serializeInteger(-2);  // key 不存在
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int64_t remaining_ttl = it->second.getRemainingTTL();
                            auto response = RESPSerializer::serializeInteger(remaining_ttl);
                            conn->send(response.data(), response.size());
                            return;
                        }
                    }
                    
                    if (cmd_name == "PTTL" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end()) {
                                auto response = RESPSerializer::serializeInteger(-2);  // key 不存在
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            int64_t remaining_ttl = it->second.getRemainingTTL();
                            if (remaining_ttl == NO_TTL) {
                                auto response = RESPSerializer::serializeInteger(-1);  // 永不过期
                                conn->send(response.data(), response.size());
                            } else if (remaining_ttl == TTL_EXPIRED) {
                                auto response = RESPSerializer::serializeInteger(-2);  // 已过期
                                conn->send(response.data(), response.size());
                            } else {
                                // 转换为毫秒
                                auto response = RESPSerializer::serializeInteger(remaining_ttl * 1000);
                                conn->send(response.data(), response.size());
                            }
                            return;
                        }
                    }
                    
                    if (cmd_name == "PERSIST" && cmd_array.size() >= 2) {
                        if (cmd_array[1] && cmd_array[1]->getType() == RESPType::BULK_STRING) {
                            auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
                            std::string key = key_bulk->getValue();
                            
                            std::lock_guard<std::mutex> lock(multi_storage_mutex_);
                            auto it = multi_storage_.find(key);
                            if (it == multi_storage_.end()) {
                                auto response = RESPSerializer::serializeInteger(0);
                                conn->send(response.data(), response.size());
                                return;
                            }
                            
                            bool had_ttl = it->second.ttl_seconds != NO_TTL;
                            it->second.setTTL(NO_TTL);
                            auto response = RESPSerializer::serializeInteger(had_ttl ? 1 : 0);
                            conn->send(response.data(), response.size());
                            return;
                        }
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
                    auto pong = RESPSerializer::serializeSimpleString("PONG");
                    conn->send(pong.data(), pong.size());
                    return;
                }
            }
        }
        
        // 如果不支持的命令，返回错误
        auto error = RESPSerializer::serializeError("Unsupported command");
        conn->send(error.data(), error.size());
        
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

void Server::ttlCleanupThread() {
    
    while (ttl_cleanup_running_) {
        try {
            cleanupExpiredKeys();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: TTL cleanup thread error: " << e.what() << std::endl;
        }
        
        // 每5秒清理一次
        std::this_thread::sleep_for(std::chrono::seconds(TTL_CLEANUP_INTERVAL_SECONDS));
    }
    
}

void Server::cleanupExpiredKeys() {
    std::lock_guard<std::mutex> lock(multi_storage_mutex_);
    
    
    auto it = multi_storage_.begin();
    while (it != multi_storage_.end()) {
        if (it->second.isExpired()) {
            it = multi_storage_.erase(it);
            expired_keys_cleaned_++;
        } else {
            ++it;
        }
    }
}

void Server::gracefulShutdown() {
    LOG_INFO("Starting graceful shutdown...");
    std::cerr << "DEBUG: gracefulShutdown() started" << std::endl;
    
    // 1. 停止接受新连接
    if (tcp_server_) {
        LOG_INFO("Stopping TCP server...");
        std::cerr << "DEBUG: Stopping TCP server..." << std::endl;
        tcp_server_->stop();
        std::cerr << "DEBUG: TCP server stopped" << std::endl;
    }
    
    // 2. 停止主事件循环
    if (main_loop_) {
        LOG_INFO("Stopping main event loop...");
        std::cerr << "DEBUG: Stopping main event loop..." << std::endl;
        main_loop_->quit();
        std::cerr << "DEBUG: Main event loop stopped" << std::endl;
    }
    
    // 3. 等待所有连接关闭（最多等待30秒）
    int wait_count = 0;
    const int max_wait_count = 300; // 30秒，每次100ms
    std::cerr << "DEBUG: Waiting for connections to close, current=" 
              << current_connections_.load() << std::endl;
    while (current_connections_.load() > 0 && wait_count < max_wait_count) {
        LOG_INFO("Waiting for {} connections to close... ({}/30s)", 
                 current_connections_.load(), wait_count / 10);
        std::cerr << "DEBUG: Still waiting, connections=" << current_connections_.load() 
                  << ", count=" << wait_count << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }
    
    std::cerr << "DEBUG: Finished waiting for connections" << std::endl;
    
    if (current_connections_.load() > 0) {
        LOG_WARN("Force closing remaining {} connections", current_connections_.load());
        std::cerr << "DEBUG: Force closing remaining connections" << std::endl;
    }
    
    // 4. 停止线程池
    if (thread_pool_) {
        LOG_INFO("Stopping thread pool...");
        std::cerr << "DEBUG: Stopping thread pool..." << std::endl;
        thread_pool_->stop();
        std::cerr << "DEBUG: Thread pool stopped" << std::endl;
    }
    
    // 5. 停止 TTL 清理线程
    if (ttl_cleanup_thread_.joinable()) {
        LOG_INFO("Stopping TTL cleanup thread...");
        std::cerr << "DEBUG: Stopping TTL cleanup thread..." << std::endl;
        ttl_cleanup_running_.store(false);
        ttl_cleanup_thread_.join();
        std::cerr << "DEBUG: TTL cleanup thread stopped" << std::endl;
    }
    
    // 6. 创建最终快照（使用多类型数据）
    if (snapshot_manager_ && storage_engine_) {
        LOG_INFO("Creating final snapshot...");
        std::cerr << "DEBUG: Creating final snapshot..." << std::endl;
        if (create_multi_type_snapshot()) {
            LOG_INFO("Final snapshot created successfully");
            std::cerr << "DEBUG: Final snapshot created successfully" << std::endl;
        } else {
            LOG_WARN("Failed to create final snapshot");
            std::cerr << "DEBUG: Failed to create final snapshot" << std::endl;
        }
    }
    
    // 7. 同步并关闭 WAL
    if (wal_manager_) {
        LOG_INFO("Syncing WAL...");
        std::cerr << "DEBUG: Syncing WAL..." << std::endl;
        if (wal_manager_->flush()) {
            LOG_INFO("WAL synced successfully");
            std::cerr << "DEBUG: WAL synced successfully" << std::endl;
        } else {
            LOG_ERROR("Failed to sync WAL");
            std::cerr << "DEBUG: Failed to sync WAL" << std::endl;
        }
    }
    
    // 8. 清理存储引擎
    if (storage_engine_) {
        LOG_INFO("Cleaning up storage engine...");
        std::cerr << "DEBUG: Cleaning up storage engine..." << std::endl;
        storage_engine_->cleanup();
        std::cerr << "DEBUG: Storage engine cleanup completed" << std::endl;
    }
    
    LOG_INFO("Graceful shutdown completed");
    std::cerr << "DEBUG: gracefulShutdown() completed" << std::endl;
}

void Server::updateStats() {
    // 这里可以定期更新统计信息
    // 比如计算 QPS、内存使用等
}
