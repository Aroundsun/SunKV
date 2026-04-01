#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <getopt.h>
#include <filesystem>

Config::Config() {
    setDefaults();
}

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file: {}", filename);
        return false;
    }
    
    LOG_INFO("Loading config from: {}", filename);
    
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        parseLine(line);
    }
    
    LOG_INFO("Config loaded successfully, {} lines processed", line_num);
    return true;
}

void Config::loadFromCommandLine(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"config", required_argument, 0, 'c'},
        {"bind", required_argument, 0, 'b'},
        {"port", required_argument, 0, 'p'},
        {"threads", required_argument, 0, 't'},
        {"data-dir", required_argument, 0, 'd'},
        {"log-level", required_argument, 0, 'l'},
        {"daemon", no_argument, 0, 'D'},
        {0, 0, 0, 0}
    };
    
    int option_index = 0;
    int c;
    
    while ((c = getopt_long(argc, argv, "hvc:b:p:t:d:l:D", long_options, &option_index)) != -1) {
        switch (c) {
            case 'h':
                showHelp();
                exit(0);
            case 'v':
                showVersion();
                exit(0);
            case 'c':
                loadFromFile(optarg);
                break;
            case 'b':
                bind_address = optarg;
                break;
            case 'p':
                bind_port = static_cast<uint16_t>(std::stoi(optarg));
                break;
            case 't':
                thread_pool_size = std::stoi(optarg);
                break;
            case 'd':
                data_dir = optarg;
                break;
            case 'l':
                log_level = optarg;
                break;
            case 'D':
                daemon_mode = true;
                break;
            case '?':
                LOG_ERROR("Unknown option. Use -h for help.");
                exit(1);
            default:
                break;
        }
    }
}

void Config::showHelp() {
    std::cout << "SunKV - High Performance Key-Value Storage Server\n\n";
    std::cout << "Usage: sunkv [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  -v, --version           Show version information\n";
    std::cout << "  -c, --config FILE       Load configuration from file\n";
    std::cout << "  -b, --bind ADDRESS     Bind to specific address (default: 0.0.0.0)\n";
    std::cout << "  -p, --port PORT         Bind to specific port (default: 6379)\n";
    std::cout << "  -t, --threads NUM       Number of worker threads (default: 4)\n";
    std::cout << "  -d, --data-dir DIR      Data directory (default: ./data)\n";
    std::cout << "  -l, --log-level LEVEL  Log level: debug, info, warn, error (default: info)\n";
    std::cout << "  -D, --daemon            Run in daemon mode\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  sunkv -c /etc/sunkv.conf -p 6380\n";
    std::cout << "  sunkv --bind 127.0.0.1 --port 6379 --threads 8\n";
}

void Config::showVersion() {
    std::cout << "SunKV version 1.0.0\n";
    std::cout << "Built with C++" << __cplusplus << "\n";
}

bool Config::validate() const {
    // 验证网络配置
    if (bind_port < 1 || bind_port > 65535) {
        LOG_ERROR("Invalid port number: {}", bind_port);
        return false;
    }
    
    if (thread_pool_size < 1 || thread_pool_size > 64) {
        LOG_ERROR("Invalid thread pool size: {}", thread_pool_size);
        return false;
    }
    
    if (max_connections < 1) {
        LOG_ERROR("Invalid max connections: {}", max_connections);
        return false;
    }
    
    // 验证存储配置
    if (shard_count < 1 || shard_count > 1024) {
        LOG_ERROR("Invalid shard count: {}", shard_count);
        return false;
    }
    
    if (cache_size < 100 || cache_size > 100000) {
        LOG_ERROR("Invalid cache size: {}", cache_size);
        return false;
    }
    
    // 验证持久化配置
    if (wal_max_file_size < 1024 * 1024) {  // 最小 1MB
        LOG_ERROR("Invalid WAL max file size: {}", wal_max_file_size);
        return false;
    }
    
    if (snapshot_interval < 60) {  // 最小 1 分钟
        LOG_ERROR("Invalid snapshot interval: {}", snapshot_interval);
        return false;
    }
    
    // 验证日志配置
    std::vector<std::string> valid_levels = {"debug", "info", "warn", "error"};
    if (std::find(valid_levels.begin(), valid_levels.end(), log_level) == valid_levels.end()) {
        LOG_ERROR("Invalid log level: {}", log_level);
        return false;
    }
    
    return true;
}

bool Config::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file for writing: {}", filename);
        return false;
    }
    
    file << "# SunKV Configuration File\n";
    file << "# Generated automatically\n\n";
    
    file << "# Network Configuration\n";
    file << "bind_address=" << bind_address << "\n";
    file << "bind_port=" << bind_port << "\n";
    file << "thread_pool_size=" << thread_pool_size << "\n";
    file << "max_connections=" << max_connections << "\n";
    file << "connection_timeout=" << connection_timeout << "\n";
    file << "keepalive_timeout=" << keepalive_timeout << "\n\n";
    
    file << "# Storage Configuration\n";
    file << "shard_count=" << shard_count << "\n";
    file << "cache_policy=" << cache_policy << "\n";
    file << "cache_size=" << cache_size << "\n";
    file << "default_ttl=" << default_ttl << "\n\n";
    
    file << "# Persistence Configuration\n";
    file << "data_dir=" << data_dir << "\n";
    file << "wal_dir=" << wal_dir << "\n";
    file << "snapshot_dir=" << snapshot_dir << "\n";
    file << "wal_max_file_size=" << wal_max_file_size << "\n";
    file << "snapshot_interval=" << snapshot_interval << "\n";
    file << "sync_mode=" << sync_mode << "\n\n";
    
    file << "# Logging Configuration\n";
    file << "log_level=" << log_level << "\n";
    file << "log_file=" << log_file << "\n";
    file << "log_rotate=" << (log_rotate ? "true" : "false") << "\n";
    file << "log_max_size=" << log_max_size << "\n\n";
    
    file << "# Performance Configuration\n";
    file << "enable_metrics=" << (enable_metrics ? "true" : "false") << "\n";
    file << "metrics_interval=" << metrics_interval << "\n";
    file << "enable_profiling=" << (enable_profiling ? "true" : "false") << "\n\n";
    
    file << "# Security Configuration\n";
    file << "require_auth=" << (require_auth ? "true" : "false") << "\n";
    file << "auth_password=" << auth_password << "\n";
    file << "enable_tls=" << (enable_tls ? "true" : "false") << "\n";
    file << "cert_file=" << cert_file << "\n";
    file << "key_file=" << key_file << "\n\n";
    
    file << "# Management Configuration\n";
    file << "pid_file=" << pid_file << "\n";
    file << "unix_socket=" << unix_socket << "\n";
    file << "daemon_mode=" << (daemon_mode ? "true" : "false") << "\n";
    
    LOG_INFO("Configuration saved to: {}", filename);
    return true;
}

void Config::parseLine(const std::string& line) {
    // 跳过空行和注释
    if (line.empty() || line[0] == '#') {
        return;
    }
    
    // 查找等号
    size_t pos = line.find('=');
    if (pos == std::string::npos) {
        return;
    }
    
    // 提取键值对
    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    
    // 去除前后空格
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    parseKeyValue(key, value);
}

void Config::parseKeyValue(const std::string& key, const std::string& value) {
    if (key == "bind_address") {
        bind_address = value;
    } else if (key == "bind_port") {
        bind_port = static_cast<uint16_t>(std::stoi(value));
    } else if (key == "thread_pool_size") {
        thread_pool_size = std::stoi(value);
    } else if (key == "max_connections") {
        max_connections = std::stoi(value);
    } else if (key == "connection_timeout") {
        connection_timeout = std::stoi(value);
    } else if (key == "keepalive_timeout") {
        keepalive_timeout = std::stoi(value);
    } else if (key == "shard_count") {
        shard_count = std::stoi(value);
    } else if (key == "cache_policy") {
        cache_policy = value;
    } else if (key == "cache_size") {
        cache_size = stringToSize(value);
    } else if (key == "default_ttl") {
        default_ttl = std::stoll(value);
    } else if (key == "data_dir") {
        data_dir = value;
    } else if (key == "wal_dir") {
        wal_dir = value;
    } else if (key == "snapshot_dir") {
        snapshot_dir = value;
    } else if (key == "wal_max_file_size") {
        wal_max_file_size = stringToSize(value);
    } else if (key == "snapshot_interval") {
        snapshot_interval = std::stoi(value);
    } else if (key == "sync_mode") {
        sync_mode = std::stoi(value);
    } else if (key == "log_level") {
        log_level = value;
    } else if (key == "log_file") {
        log_file = value;
    } else if (key == "log_rotate") {
        log_rotate = stringToBool(value);
    } else if (key == "log_max_size") {
        log_max_size = stringToSize(value);
    } else if (key == "enable_metrics") {
        enable_metrics = stringToBool(value);
    } else if (key == "metrics_interval") {
        metrics_interval = std::stoi(value);
    } else if (key == "enable_profiling") {
        enable_profiling = stringToBool(value);
    } else if (key == "require_auth") {
        require_auth = stringToBool(value);
    } else if (key == "auth_password") {
        auth_password = value;
    } else if (key == "enable_tls") {
        enable_tls = stringToBool(value);
    } else if (key == "cert_file") {
        cert_file = value;
    } else if (key == "key_file") {
        key_file = value;
    } else if (key == "pid_file") {
        pid_file = value;
    } else if (key == "unix_socket") {
        unix_socket = value;
    } else if (key == "daemon_mode") {
        daemon_mode = stringToBool(value);
    } else {
        LOG_WARN("Unknown configuration key: {}", key);
    }
}

void Config::setDefaults() {
    // 设置默认值已在成员变量初始化时完成
}

bool Config::createDirectories() const {
    try {
        std::filesystem::create_directories(data_dir);
        std::filesystem::create_directories(wal_dir);
        std::filesystem::create_directories(snapshot_dir);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create directories: {}", e.what());
        return false;
    }
}

bool Config::stringToBool(const std::string& str) const {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    return lower_str == "true" || lower_str == "yes" || lower_str == "1";
}

int Config::stringToInt(const std::string& str, int default_val) const {
    try {
        return std::stoi(str);
    } catch (...) {
        return default_val;
    }
}

size_t Config::stringToSize(const std::string& str, size_t default_val) const {
    try {
        return static_cast<size_t>(std::stoull(str));
    } catch (...) {
        return default_val;
    }
}
