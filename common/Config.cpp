#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <functional>
#include "../network/logger.h"

namespace {

enum class ValueType {
    String,
    Int,
    Bool,
};

struct OptionSpec {
    std::string key;         // config file key: e.g. "port"
    std::string group;       // for sample/usage grouping only
    std::string cli_flag;    // e.g. "--port" (empty means no cli)
    ValueType type;
    std::string default_value;
    std::string description; // one-line description

    std::function<bool(Config&, const std::string&)> set_from_string;
    std::function<std::string(const Config&)> get_as_string;
};

static std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool parseBoolLoose(const std::string& raw, bool* out) {
    std::string s = lower(trim(raw));
    if (s == "true" || s == "1" || s == "yes" || s == "on") { *out = true; return true; }
    if (s == "false" || s == "0" || s == "no" || s == "off") { *out = false; return true; }
    return false;
}

static bool parseIntLoose(const std::string& raw, int* out) {
    try {
        size_t idx = 0;
        std::string s = trim(raw);
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

static const std::vector<OptionSpec>& specs() {
    static const std::vector<OptionSpec> k = {
        // network
        {"host", "network", "--host", ValueType::String, "0.0.0.0", "Server bind host",
            [](Config& c, const std::string& v) { c.host = v; return true; },
            [](const Config& c) { return c.host; }},
        {"port", "network", "--port", ValueType::Int, "6379", "Server port",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.port = x; return true; },
            [](const Config& c) { return std::to_string(c.port); }},
        {"max_connections", "network", "--max-connections", ValueType::Int, "1000", "Max client connections",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.max_connections = x; return true; },
            [](const Config& c) { return std::to_string(c.max_connections); }},
        {"thread_pool_size", "network", "--thread-pool-size", ValueType::Int, "4", "Worker thread count",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.thread_pool_size = x; return true; },
            [](const Config& c) { return std::to_string(c.thread_pool_size); }},

        // storage
        {"data_dir", "storage", "--data-dir", ValueType::String, "./data", "Data directory",
            [](Config& c, const std::string& v) { c.data_dir = v; return true; },
            [](const Config& c) { return c.data_dir; }},
        {"wal_dir", "storage", "--wal-dir", ValueType::String, "./data/wal", "WAL directory",
            [](Config& c, const std::string& v) { c.wal_dir = v; return true; },
            [](const Config& c) { return c.wal_dir; }},
        {"snapshot_dir", "storage", "--snapshot-dir", ValueType::String, "./data/snapshot", "Snapshot directory",
            [](Config& c, const std::string& v) { c.snapshot_dir = v; return true; },
            [](const Config& c) { return c.snapshot_dir; }},
        {"max_memory_mb", "storage", "--max-memory-mb", ValueType::Int, "1024", "Max memory (MB)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.max_memory_mb = x; return true; },
            [](const Config& c) { return std::to_string(c.max_memory_mb); }},

        // persistence
        {"enable_wal", "persistence", "--enable-wal", ValueType::Bool, "true", "Enable WAL",
            [](Config& c, const std::string& v) { bool b; if (!parseBoolLoose(v, &b)) return false; c.enable_wal = b; return true; },
            [](const Config& c) { return c.enable_wal ? "true" : "false"; }},
        {"enable_snapshot", "persistence", "--enable-snapshot", ValueType::Bool, "true", "Enable snapshot",
            [](Config& c, const std::string& v) { bool b; if (!parseBoolLoose(v, &b)) return false; c.enable_snapshot = b; return true; },
            [](const Config& c) { return c.enable_snapshot ? "true" : "false"; }},
        {"snapshot_interval_seconds", "persistence", "--snapshot-interval-seconds", ValueType::Int, "3600", "Snapshot interval (seconds)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.snapshot_interval_seconds = x; return true; },
            [](const Config& c) { return std::to_string(c.snapshot_interval_seconds); }},
        {"wal_sync_interval_ms", "persistence", "--wal-sync-interval-ms", ValueType::Int, "100", "WAL sync interval (ms)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.wal_sync_interval_ms = x; return true; },
            [](const Config& c) { return std::to_string(c.wal_sync_interval_ms); }},
        {"max_wal_file_size_mb", "persistence", "--max-wal-file-size-mb", ValueType::Int, "100", "Max WAL file size (MB)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.max_wal_file_size_mb = x; return true; },
            [](const Config& c) { return std::to_string(c.max_wal_file_size_mb); }},
        {"wal_async", "persistence", "--wal-async", ValueType::Bool, "true", "Async WAL submit queue",
            [](Config& c, const std::string& v) { bool b; if (!parseBoolLoose(v, &b)) return false; c.wal_async = b; return true; },
            [](const Config& c) { return c.wal_async ? "true" : "false"; }},
        {"wal_max_queue", "persistence", "--wal-max-queue", ValueType::Int, "100000", "Max WAL async queue depth (batches)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.wal_max_queue = x; return true; },
            [](const Config& c) { return std::to_string(c.wal_max_queue); }},
        {"wal_flush_policy", "persistence", "--wal-flush-policy", ValueType::String, "periodic", "never|always|periodic",
            [](Config& c, const std::string& v) { c.wal_flush_policy = v; return true; },
            [](const Config& c) { return c.wal_flush_policy; }},
        {"wal_group_commit_linger_ms", "persistence", "--wal-group-commit-linger-ms", ValueType::Int, "2", "WAL group commit linger (ms)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.wal_group_commit_linger_ms = x; return true; },
            [](const Config& c) { return std::to_string(c.wal_group_commit_linger_ms); }},
        {"wal_group_commit_max_mutations", "persistence", "--wal-group-commit-max-mutations", ValueType::Int, "8192", "WAL group max mutations per write",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.wal_group_commit_max_mutations = x; return true; },
            [](const Config& c) { return std::to_string(c.wal_group_commit_max_mutations); }},
        {"wal_group_commit_max_bytes", "persistence", "--wal-group-commit-max-bytes", ValueType::Int, "2097152", "WAL group max encoded bytes per write",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.wal_group_commit_max_bytes = x; return true; },
            [](const Config& c) { return std::to_string(c.wal_group_commit_max_bytes); }},

        // logging
        {"log_level", "logging", "--log-level", ValueType::String, "INFO", "DEBUG|INFO|WARN|ERROR (Debug 构建未指定时默认为 DEBUG)",
            [](Config& c, const std::string& v) { c.log_level = v; return true; },
            [](const Config& c) { return c.log_level; }},
        {"log_file", "logging", "--log-file", ValueType::String, "./data/logs/sunkv.log", "Log file path",
            [](Config& c, const std::string& v) { c.log_file = v; return true; },
            [](const Config& c) { return c.log_file; }},
        {"log_strategy", "logging", "--log-strategy", ValueType::String, "fixed", "fixed|per_run|daily",
            [](Config& c, const std::string& v) { c.log_strategy = v; return true; },
            [](const Config& c) { return c.log_strategy; }},
        {"enable_console_log", "logging", "--enable-console-log", ValueType::Bool, "true", "Enable console log",
            [](Config& c, const std::string& v) { bool b; if (!parseBoolLoose(v, &b)) return false; c.enable_console_log = b; return true; },
            [](const Config& c) { return c.enable_console_log ? "true" : "false"; }},
        {"enable_periodic_stats_log", "logging", "--enable-periodic-stats-log", ValueType::Bool, "false", "Enable periodic stats log",
            [](Config& c, const std::string& v) { bool b; if (!parseBoolLoose(v, &b)) return false; c.enable_periodic_stats_log = b; return true; },
            [](const Config& c) { return c.enable_periodic_stats_log ? "true" : "false"; }},
        {"stats_log_interval_seconds", "logging", "--stats-log-interval", ValueType::Int, "30", "Stats log interval (seconds)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.stats_log_interval_seconds = x; return true; },
            [](const Config& c) { return std::to_string(c.stats_log_interval_seconds); }},

        // ttl
        {"ttl_cleanup_interval_seconds", "ttl", "--ttl-cleanup-interval-seconds", ValueType::Int, "5", "TTL cleanup interval (seconds)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.ttl_cleanup_interval_seconds = x; return true; },
            [](const Config& c) { return std::to_string(c.ttl_cleanup_interval_seconds); }},
        {"max_ttl_seconds", "ttl", "--max-ttl-seconds", ValueType::Int, "2592000", "Max TTL (seconds)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.max_ttl_seconds = x; return true; },
            [](const Config& c) { return std::to_string(c.max_ttl_seconds); }},

        // performance
        {"tcp_keepalive_seconds", "performance", "--tcp-keepalive-seconds", ValueType::Int, "300", "TCP keepalive seconds",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.tcp_keepalive_seconds = x; return true; },
            [](const Config& c) { return std::to_string(c.tcp_keepalive_seconds); }},
        {"tcp_send_buffer_size", "performance", "--tcp-send-buffer-size", ValueType::Int, "65536", "TCP send buffer size (bytes)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.tcp_send_buffer_size = x; return true; },
            [](const Config& c) { return std::to_string(c.tcp_send_buffer_size); }},
        {"tcp_recv_buffer_size", "performance", "--tcp-recv-buffer-size", ValueType::Int, "65536", "TCP recv buffer size (bytes)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.tcp_recv_buffer_size = x; return true; },
            [](const Config& c) { return std::to_string(c.tcp_recv_buffer_size); }},
        {"max_conn_input_buffer_mb", "performance", "--max-conn-input-buffer-mb", ValueType::Int, "8", "Per-connection input buffer cap (MB)",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.max_conn_input_buffer_mb = x; return true; },
            [](const Config& c) { return std::to_string(c.max_conn_input_buffer_mb); }},
        {"memory_pool_max_cached_blocks_per_size", "performance", "--memory-pool-max-cached-blocks-per-size", ValueType::Int, "8",
            "ThreadLocalBufferPool max cached blocks per size class",
            [](Config& c, const std::string& v) { int x; if (!parseIntLoose(v, &x)) return false; c.memory_pool_max_cached_blocks_per_size = x; return true; },
            [](const Config& c) { return std::to_string(c.memory_pool_max_cached_blocks_per_size); }},
    };
    return k;
}

static const std::unordered_map<std::string, const OptionSpec*>& specByKey() {
    static std::unordered_map<std::string, const OptionSpec*> m;
    if (m.empty()) {
        for (const auto& s : specs()) m.emplace(s.key, &s);
    }
    return m;
}

static const std::unordered_map<std::string, const OptionSpec*>& specByFlag() {
    static std::unordered_map<std::string, const OptionSpec*> m;
    if (m.empty()) {
        for (const auto& s : specs()) {
            if (!s.cli_flag.empty()) m.emplace(s.cli_flag, &s);
        }
    }
    return m;
}

} // namespace

Config& Config::getInstance() {
    static Config instance;
    instance.initSchemaDefaultsOnce();
    return instance;
}

void Config::initSchemaDefaultsOnce() {
    static bool inited = false;
    if (inited) return;
    inited = true;
    for (const auto& s : specs()) {
        (void)setFromKeyValue(s.key, s.default_value, Source::Default);
    }
}

bool Config::setFromKeyValue(const std::string& key, const std::string& value, Source src) {
    auto it = specByKey().find(key);
    if (it == specByKey().end()) {
        LOG_WARN("Unknown config key ignored: {}={}", key, value);
        return false;
    }

    // 来源优先级：CLI 覆盖文件覆盖默认
    auto src_it = source_map_.find(key);
    if (src_it != source_map_.end()) {
        if (static_cast<int>(src) < static_cast<int>(src_it->second)) {
            return true; // lower priority ignored
        }
    }

    const OptionSpec* spec = it->second;
    if (!spec->set_from_string(*this, value)) {
        LOG_ERROR("Invalid value for key '{}': '{}'", key, value);
        return false;
    }
    source_map_[key] = src;
    return true;
}

bool Config::loadFromFile(const std::string& filename) {
    initSchemaDefaultsOnce();
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open config file: {}", filename);
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
            LOG_ERROR("Error parsing line {}: {}", line_number, e.what());
            return false;
        }
    }
    
    // 应用配置到成员变量（schema 驱动）
    for (const auto& kv : config_map_) {
        (void)setFromKeyValue(kv.first, kv.second, Source::ConfigFile);
    }
    
    LOG_INFO("Config loaded from {}", filename);
    return true;
}

bool Config::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to create config file: {}", filename);
        return false;
    }

    file << generateSampleConfig();
    
    LOG_INFO("Config saved to {}", filename);
    return true;
}

void Config::loadFromArgs(int argc, char* argv[]) {
    initSchemaDefaultsOnce();

    // 固定管线：default → config file(可选且只加载一次) → CLI
    std::string config_path;
    int config_count = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[i + 1] ? argv[i + 1] : "";
            ++config_count;
            ++i;
        }
    }
    if (config_count > 1) {
        LOG_WARN("--config 出现多次（{} 次），将使用最后一次: {}", config_count, config_path);
    }
    if (!config_path.empty()) {
        (void)loadFromFile(config_path);
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--config" && i + 1 < argc) {
            ++i; // 已在前置阶段处理
            continue;
        }
        if (arg == "--help") {
            printUsage();
            exit(0);
        }

        auto fit = specByFlag().find(arg);
        if (fit == specByFlag().end()) {
            continue;
        }
        if (i + 1 >= argc) {
            LOG_ERROR("Missing value for arg: {}", arg);
            continue;
        }
        const char* v = argv[++i];
        const std::string value = v ? v : "";
        (void)setFromKeyValue(fit->second->key, value, Source::CommandLine);
        if (fit->second->key == "log_level") {
            log_level_from_cli = true;
        }
    }
}

void Config::applyBuildDefaults() {
    initSchemaDefaultsOnce();
#ifndef NDEBUG
    // Debug 构建：仅在“未通过文件/CLI 显式设置”时才提升到 DEBUG
    auto it = source_map_.find("log_level");
    const bool has_user_value = (it != source_map_.end() && it->second != Source::Default);
    if (!has_user_value && !log_level_from_cli) {
        (void)setFromKeyValue("log_level", "DEBUG", Source::Default);
    }
#endif
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
            LOG_ERROR("Invalid integer value for key: {}", key);
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
        LOG_ERROR("Invalid port: {} (must be 1-65535)", port);
        valid = false;
    }
    
    if (max_connections < 0) {
        LOG_ERROR("Invalid max_connections: {} (use 0 for unlimited)", max_connections);
        valid = false;
    }
    
    if (thread_pool_size <= 0) {
        LOG_ERROR("Invalid thread_pool_size: {}", thread_pool_size);
        valid = false;
    }
    
    if (max_memory_mb < 0) {
        LOG_ERROR("Invalid max_memory_mb: {} (use 0 for unlimited)", max_memory_mb);
        valid = false;
    }
    
    if (enable_snapshot && snapshot_interval_seconds <= 0) {
        LOG_ERROR("Invalid snapshot_interval_seconds: {}", snapshot_interval_seconds);
        valid = false;
    }
    
    if (enable_periodic_stats_log && stats_log_interval_seconds <= 0) {
        LOG_ERROR("Invalid stats_log_interval_seconds: {}", stats_log_interval_seconds);
        valid = false;
    }

    if (ttl_cleanup_interval_seconds <= 0) {
        LOG_ERROR("Invalid ttl_cleanup_interval_seconds: {}", ttl_cleanup_interval_seconds);
        valid = false;
    }
    if (max_ttl_seconds <= 0) {
        LOG_ERROR("Invalid max_ttl_seconds: {}", max_ttl_seconds);
        valid = false;
    }
    if (wal_max_queue < 0) {
        LOG_ERROR("Invalid wal_max_queue: {}", wal_max_queue);
        valid = false;
    }
    if (wal_group_commit_linger_ms < 0) {
        LOG_ERROR("Invalid wal_group_commit_linger_ms: {}", wal_group_commit_linger_ms);
        valid = false;
    }
    if (wal_group_commit_max_mutations <= 0) {
        LOG_ERROR("Invalid wal_group_commit_max_mutations: {}", wal_group_commit_max_mutations);
        valid = false;
    }
    if (wal_group_commit_max_bytes <= 0) {
        LOG_ERROR("Invalid wal_group_commit_max_bytes: {}", wal_group_commit_max_bytes);
        valid = false;
    }
    if (max_wal_file_size_mb < 0) {
        LOG_ERROR("Invalid max_wal_file_size_mb: {}", max_wal_file_size_mb);
        valid = false;
    }
    {
        std::string p = lower(trim(wal_flush_policy));
        if (p != "never" && p != "always" && p != "periodic") {
            LOG_ERROR("Invalid wal_flush_policy: {} (never|always|periodic)", wal_flush_policy);
            valid = false;
        }
    }
    if (max_conn_input_buffer_mb <= 0) {
        LOG_ERROR("Invalid max_conn_input_buffer_mb: {}", max_conn_input_buffer_mb);
        valid = false;
    }
    if (memory_pool_max_cached_blocks_per_size <= 0) {
        LOG_ERROR("Invalid memory_pool_max_cached_blocks_per_size: {}", memory_pool_max_cached_blocks_per_size);
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
    std::cout << "  Periodic Stats Log: " << (enable_periodic_stats_log ? "Yes" : "No") << std::endl;
    std::cout << "  Stats Log Interval: " << stats_log_interval_seconds << " s" << std::endl;
    
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
    // 历史遗留：之前 parseLine()->config_map_ 再手工 apply 到成员变量。
    // 现在用 schema 的 setFromKeyValue() 直接落到成员变量，该函数保持为空以避免旧路径误用。
}

void Config::printUsage() const {
    std::cout << "SunKV - High Performance Key-Value Store" << std::endl;
    std::cout << "Usage: sunkv [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --config <file>         Configuration file" << std::endl;

    std::string last_group;
    for (const auto& s : specs()) {
        if (s.cli_flag.empty()) continue;
        if (s.group != last_group) {
            std::cout << std::endl;
            std::cout << "  [" << s.group << "]" << std::endl;
            last_group = s.group;
        }
        std::cout << "  " << s.cli_flag << " <value>"
                  << "    " << s.description
                  << " (default: " << s.default_value << ")"
                  << std::endl;
    }
    std::cout << "  --help                  Show this help message" << std::endl;
}

std::string Config::generateSampleConfig() const {
    std::ostringstream out;
    out << "# SunKV Configuration File\n";
    out << "# NOTE: 该配置文件语义是“扁平 key=value”。这里的分组仅用于阅读。\n";
    out << "#       CLI 优先级高于配置文件；配置文件优先级高于内置默认值。\n";
    out << "# Generated automatically\n\n";

    std::string last_group;
    for (const auto& s : specs()) {
        if (s.group != last_group) {
            out << "\n# [" << s.group << "]\n";
            last_group = s.group;
        }
        out << "# " << s.description << " (default: " << s.default_value << ")\n";
        out << s.key << " = " << s.get_as_string(*this) << "\n";
    }
    out << "\n";
    return out.str();
}

std::string Config::dumpEffectiveConfigWithSource() const {
    auto srcName = [](Source s) -> const char* {
        switch (s) {
            case Source::Default: return "default";
            case Source::ConfigFile: return "file";
            case Source::CommandLine: return "cli";
        }
        return "unknown";
    };

    std::ostringstream out;
    out << "=== Effective config ===\n";
    std::string last_group;
    for (const auto& s : specs()) {
        if (s.group != last_group) {
            out << "\n[" << s.group << "]\n";
            last_group = s.group;
        }
        Source src = Source::Default;
        auto it = source_map_.find(s.key);
        if (it != source_map_.end()) src = it->second;
        out << s.key << " = " << s.get_as_string(*this) << "    (" << srcName(src) << ")\n";
    }
    out << "\n";
    return out.str();
}
