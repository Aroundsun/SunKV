#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        try {
            parseLine(line);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << line_number << ": " << e.what() << std::endl;
            return false;
        }
    }
    
    // 应用配置到成员变量
    applyConfig();
    
    std::cout << "Config loaded from " << filename << std::endl;
    return true;
}

bool Config::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to create config file: " << filename << std::endl;
        return false;
    }
    
    file << "# SunKV Configuration File\n";
    file << "# Generated automatically\n\n";
    
    file << "[network]\n";
    file << "host = " << host << "\n";
    file << "port = " << port << "\n";
    file << "max_connections = " << max_connections << "\n";
    file << "thread_pool_size = " << thread_pool_size << "\n\n";
    
    file << "[storage]\n";
    file << "data_dir = " << data_dir << "\n";
    file << "wal_dir = " << wal_dir << "\n";
    file << "snapshot_dir = " << snapshot_dir << "\n";
    file << "max_memory_mb = " << max_memory_mb << "\n\n";
    
    file << "[persistence]\n";
    file << "enable_wal = " << (enable_wal ? "true" : "false") << "\n";
    file << "enable_snapshot = " << (enable_snapshot ? "true" : "false") << "\n";
    file << "snapshot_interval_seconds = " << snapshot_interval_seconds << "\n";
    file << "wal_sync_interval_ms = " << wal_sync_interval_ms << "\n";
    file << "max_wal_file_size_mb = " << max_wal_file_size_mb << "\n\n";
    
    file << "[logging]\n";
    file << "log_level = " << log_level << "\n";
    file << "log_file = " << log_file << "\n";
    file << "enable_console_log = " << (enable_console_log ? "true" : "false") << "\n\n";
    
    file << "[ttl]\n";
    file << "ttl_cleanup_interval_seconds = " << ttl_cleanup_interval_seconds << "\n";
    file << "max_ttl_seconds = " << max_ttl_seconds << "\n\n";
    
    file << "[performance]\n";
    file << "tcp_keepalive_seconds = " << tcp_keepalive_seconds << "\n";
    file << "tcp_send_buffer_size = " << tcp_send_buffer_size << "\n";
    file << "tcp_recv_buffer_size = " << tcp_recv_buffer_size << "\n";
    
    std::cout << "Config saved to " << filename << std::endl;
    return true;
}

void Config::loadFromArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            loadFromFile(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--help") {
            printUsage();
            exit(0);
        }
    }
}

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = config_map_.find(key);
    return (it != config_map_.end()) ? it->second : defaultValue;
}

int Config::getInt(const std::string& key, int defaultValue) const {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            std::cerr << "Invalid integer value for key: " << key << std::endl;
        }
    }
    return defaultValue;
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes" || value == "on");
    }
    return defaultValue;
}

void Config::setString(const std::string& key, const std::string& value) {
    config_map_[key] = value;
}

void Config::setInt(const std::string& key, int value) {
    config_map_[key] = std::to_string(value);
}

void Config::setBool(const std::string& key, bool value) {
    config_map_[key] = value ? "true" : "false";
}

bool Config::validate() const {
    bool valid = true;
    
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port: " << port << " (must be 1-65535)" << std::endl;
        valid = false;
    }
    
    if (max_connections <= 0) {
        std::cerr << "Invalid max_connections: " << max_connections << std::endl;
        valid = false;
    }
    
    if (thread_pool_size <= 0) {
        std::cerr << "Invalid thread_pool_size: " << thread_pool_size << std::endl;
        valid = false;
    }
    
    if (max_memory_mb <= 0) {
        std::cerr << "Invalid max_memory_mb: " << max_memory_mb << std::endl;
        valid = false;
    }
    
    if (snapshot_interval_seconds <= 0) {
        std::cerr << "Invalid snapshot_interval_seconds: " << snapshot_interval_seconds << std::endl;
        valid = false;
    }
    
    return valid;
}

void Config::print() const {
    std::cout << "=== SunKV Configuration ===" << std::endl;
    std::cout << "Network:" << std::endl;
    std::cout << "  Host: " << host << std::endl;
    std::cout << "  Port: " << port << std::endl;
    std::cout << "  Max Connections: " << max_connections << std::endl;
    std::cout << "  Thread Pool Size: " << thread_pool_size << std::endl;
    
    std::cout << "Storage:" << std::endl;
    std::cout << "  Data Dir: " << data_dir << std::endl;
    std::cout << "  WAL Dir: " << wal_dir << std::endl;
    std::cout << "  Snapshot Dir: " << snapshot_dir << std::endl;
    std::cout << "  Max Memory: " << max_memory_mb << " MB" << std::endl;
    
    std::cout << "Persistence:" << std::endl;
    std::cout << "  WAL Enabled: " << (enable_wal ? "Yes" : "No") << std::endl;
    std::cout << "  Snapshot Enabled: " << (enable_snapshot ? "Yes" : "No") << std::endl;
    std::cout << "  Snapshot Interval: " << snapshot_interval_seconds << " s" << std::endl;
    std::cout << "  WAL Sync Interval: " << wal_sync_interval_ms << " ms" << std::endl;
    
    std::cout << "Logging:" << std::endl;
    std::cout << "  Log Level: " << log_level << std::endl;
    std::cout << "  Log File: " << log_file << std::endl;
    std::cout << "  Console Log: " << (enable_console_log ? "Yes" : "No") << std::endl;
    
    std::cout << "=========================" << std::endl;
}

void Config::parseLine(const std::string& line) {
    size_t equal_pos = line.find('=');
    if (equal_pos == std::string::npos) {
        return;
    }
    
    std::string key = line.substr(0, equal_pos);
    std::string value = line.substr(equal_pos + 1);
    
    // 去除空格
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    // 去除引号
    if ((value.front() == '"' && value.back() == '"') || 
        (value.front() == '\'' && value.back() == '\'')) {
        value = value.substr(1, value.length() - 2);
    }
    
    config_map_[key] = value;
}

void Config::applyConfig() {
    host = getString("host", host);
    port = getInt("port", port);
    max_connections = getInt("max_connections", max_connections);
    thread_pool_size = getInt("thread_pool_size", thread_pool_size);
    
    data_dir = getString("data_dir", data_dir);
    wal_dir = getString("wal_dir", wal_dir);
    snapshot_dir = getString("snapshot_dir", snapshot_dir);
    max_memory_mb = getInt("max_memory_mb", max_memory_mb);
    
    enable_wal = getBool("enable_wal", enable_wal);
    enable_snapshot = getBool("enable_snapshot", enable_snapshot);
    snapshot_interval_seconds = getInt("snapshot_interval_seconds", snapshot_interval_seconds);
    wal_sync_interval_ms = getInt("wal_sync_interval_ms", wal_sync_interval_ms);
    max_wal_file_size_mb = getInt("max_wal_file_size_mb", max_wal_file_size_mb);
    
    log_level = getString("log_level", log_level);
    log_file = getString("log_file", log_file);
    enable_console_log = getBool("enable_console_log", enable_console_log);
    
    ttl_cleanup_interval_seconds = getInt("ttl_cleanup_interval_seconds", ttl_cleanup_interval_seconds);
    max_ttl_seconds = getInt("max_ttl_seconds", max_ttl_seconds);
    
    tcp_keepalive_seconds = getInt("tcp_keepalive_seconds", tcp_keepalive_seconds);
    tcp_send_buffer_size = getInt("tcp_send_buffer_size", tcp_send_buffer_size);
    tcp_recv_buffer_size = getInt("tcp_recv_buffer_size", tcp_recv_buffer_size);
}

void Config::printUsage() const {
    std::cout << "SunKV - High Performance Key-Value Store" << std::endl;
    std::cout << "Usage: sunkv [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --port <port>           Server port (default: 6379)" << std::endl;
    std::cout << "  --host <host>           Server host (default: 0.0.0.0)" << std::endl;
    std::cout << "  --data-dir <dir>        Data directory (default: ./data)" << std::endl;
    std::cout << "  --config <file>         Configuration file" << std::endl;
    std::cout << "  --help                  Show this help message" << std::endl;
}
