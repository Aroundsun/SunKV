#pragma once

#include <string>
#include <map>

// 配置类
class Config {
public:
    // 网络配置
    std::string host = "0.0.0.0";
    int port = 6379;
    int max_connections = 1000;
    int thread_pool_size = 4;
    
    // 存储配置
    std::string data_dir = "./data";
    std::string wal_dir = "./data/wal";
    std::string snapshot_dir = "./data/snapshot";
    /// 0 表示不限制存储估算用量
    int max_memory_mb = 1024;
    
    // 持久化配置
    bool enable_wal = true;
    bool enable_snapshot = true;
    int snapshot_interval_seconds = 3600;  // 1小时
    int wal_sync_interval_ms = 100;        // 100ms
    int max_wal_file_size_mb = 100;

    /// WAL 异步提交（false 则同步写 WAL 并同步回调 consumers）
    bool wal_async = true;
    /// 异步队列上限（条数为 MutationBatch 个数）
    int wal_max_queue = 100000;
    /// never | always | periodic（与 PersistenceOrchestrator::WalFlushPolicy 对应）
    std::string wal_flush_policy = "periodic";
    int wal_group_commit_linger_ms = 2;
    int wal_group_commit_max_mutations = 8192;
    int wal_group_commit_max_bytes = 2097152;
    
    // 日志配置
    std::string log_level = "INFO";
    /** 是否由命令行传入过 --log-level（用于 Debug 构建下默认升級日志级别时保留用户显式指定） */
    bool log_level_from_cli = false;
    std::string log_file = "./data/logs/sunkv.log";
    // 日志文件策略：fixed（固定文件名）/ per_run（按进程启动生成独立文件）/ daily（按日期归档）
    std::string log_strategy = "fixed";
    bool enable_console_log = true;
    bool enable_periodic_stats_log = false;
    int stats_log_interval_seconds = 30;
    
    // TTL 配置
    int ttl_cleanup_interval_seconds = 5;
    int max_ttl_seconds = 86400 * 30;  // 30天
    
    // 性能配置
    int tcp_keepalive_seconds = 300;
    int tcp_send_buffer_size = 65536;
    int tcp_recv_buffer_size = 65536;

    /// 单连接 RESP 输入累积上限（MB）
    int max_conn_input_buffer_mb = 8;
    /// ThreadLocalBufferPool：每种尺寸最多缓存块数
    int memory_pool_max_cached_blocks_per_size = 8;
    
    // 单例模式
    static Config& getInstance();
    
    // 拷贝构造函数和赋值操作符
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;
    
    // 加载配置文件
    bool loadFromFile(const std::string& filename);
    
    // 保存配置文件
    bool saveToFile(const std::string& filename) const;
    
    // 从命令行参数加载
    void loadFromArgs(int argc, char* argv[]);

    // Debug 构建下的隐式默认值（仅在用户未通过文件/CLI 显式设置时生效）
    void applyBuildDefaults();
    
    // 获取配置值
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    
    // 设置配置值
    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setBool(const std::string& key, bool value);
    
    // 验证配置
    bool validate() const;
    
    // 打印配置
    void print() const;
    
    // 打印使用说明
    void printUsage() const;

    // 生成示例配置文件内容（与解析语义一致：扁平 key=value）
    std::string generateSampleConfig() const;

    // 打印一次性“最终配置摘要 + 来源（default/file/cli）”
    std::string dumpEffectiveConfigWithSource() const;
    
    // 析构函数需要公开，因为 unique_ptr 需要调用
    ~Config() = default;
    
private:
    Config() = default;
    
    std::map<std::string, std::string> config_map_;

    enum class Source {
        Default = 0,
        ConfigFile = 1,
        CommandLine = 2,
    };

    std::map<std::string, Source> source_map_;

    // schema 驱动的设置入口：做类型解析/校验，并记录来源
    bool setFromKeyValue(const std::string& key, const std::string& value, Source src);

    void initSchemaDefaultsOnce();
    
    // 解析配置行
    void parseLine(const std::string& line);
    
    // 应用配置到成员变量
    void applyConfig();
    
    // 转换为字符串
    std::string toString() const;
};
