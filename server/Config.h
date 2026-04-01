#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include "network/logger.h"

/**
 * @brief 配置管理类
 * 
 * 负责加载和管理服务器配置
 * 支持从配置文件加载和命令行参数覆盖
 */
class Config {
public:
    /**
     * @brief 构造函数
     */
    Config();
    
    /**
     * @brief 析构函数
     */
    ~Config() = default;
    
    // 禁用拷贝
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    /**
     * @brief 从配置文件加载
     * @param filename 配置文件路径
     * @return 是否加载成功
     */
    bool loadFromFile(const std::string& filename);
    
    /**
     * @brief 从命令行参数加载
     * @param argc 参数个数
     * @param argv 参数数组
     */
    void loadFromCommandLine(int argc, char* argv[]);
    
    /**
     * @brief 显示帮助信息
     */
    static void showHelp();
    
    /**
     * @brief 显示版本信息
     */
    static void showVersion();
    
    /**
     * @brief 验证配置有效性
     * @return 是否有效
     */
    bool validate() const;
    
    /**
     * @brief 保存配置到文件
     * @param filename 文件路径
     * @return 是否保存成功
     */
    bool saveToFile(const std::string& filename) const;

public:
    // 网络配置
    std::string bind_address = "0.0.0.0";     // 绑定地址
    uint16_t bind_port = 6379;                // 绑定端口
    int thread_pool_size = 4;                    // 线程池大小
    int max_connections = 10000;                 // 最大连接数
    int connection_timeout = 300;                  // 连接超时(秒)
    int keepalive_timeout = 60;                   // 保活超时(秒)
    
    // 存储配置
    int shard_count = 16;                         // 分片数量
    std::string cache_policy = "lru";            // 缓存策略
    size_t cache_size = 1000;                     // 缓存大小
    int64_t default_ttl = 0;                      // 默认TTL(0=永不过期)
    
    // 持久化配置
    std::string data_dir = "./data";              // 数据目录
    std::string wal_dir = "./data/wal";            // WAL 目录
    std::string snapshot_dir = "./data/snapshot";    // 快照目录
    size_t wal_max_file_size = 100 * 1024 * 1024;  // WAL 最大文件大小(100MB)
    int snapshot_interval = 3600;                 // 快照间隔(秒)
    int sync_mode = 0;                           // 同步模式(0=异步,1=同步)
    
    // 日志配置
    std::string log_level = "info";               // 日志级别
    std::string log_file = "";                     // 日志文件(空=控制台)
    bool log_rotate = true;                       // 日志轮换
    size_t log_max_size = 100 * 1024 * 1024;    // 日志最大文件大小
    
    // 性能配置
    bool enable_metrics = true;                    // 启用指标收集
    int metrics_interval = 60;                    // 指标收集间隔(秒)
    bool enable_profiling = false;                  // 启用性能分析
    
    // 安全配置
    bool require_auth = false;                     // 需要认证
    std::string auth_password = "";                  // 认证密码
    bool enable_tls = false;                       // 启用TLS
    std::string cert_file = "";                      // 证书文件
    std::string key_file = "";                       // 私钥文件
    
    // 管理配置
    std::string pid_file = "./data/sunkv.pid";    // PID文件
    std::string unix_socket = "";                   // Unix socket路径
    bool daemon_mode = false;                      // 守护进程模式

private:
    /**
     * @brief 解析配置行
     * @param line 配置行
     */
    void parseLine(const std::string& line);
    
    /**
     * @brief 解析键值对
     * @param key 键
     * @param value 值
     */
    void parseKeyValue(const std::string& key, const std::string& value);
    
    /**
     * @brief 设置默认值
     */
    void setDefaults();
    
    /**
     * @brief 创建必要的目录
     */
    bool createDirectories() const;
    
    /**
     * @brief 字符串转布尔值
     */
    bool stringToBool(const std::string& str) const;
    
    /**
     * @brief 字符串转整数
     */
    int stringToInt(const std::string& str, int default_val = 0) const;
    
    /**
     * @brief 字符串转大小
     */
    size_t stringToSize(const std::string& str, size_t default_val = 0) const;
};
