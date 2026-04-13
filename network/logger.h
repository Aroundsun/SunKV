    #pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

class Logger {
public:
    static Logger& instance();
    
    void setLevel(spdlog::level::level_enum level);
    spdlog::level::level_enum getLevel() const;
    
    std::shared_ptr<spdlog::logger> getLogger() const;
    
    // 新增方法
    void setLevelFromName(const std::string& level_name);
    void setFile(const std::string& filename);
    void setConsoleEnabled(bool enabled);
    void setFileStrategy(const std::string& strategy_name);
    
private:
    Logger();
    void rebuildLogger();
    std::string resolveFilePath() const;

    std::shared_ptr<spdlog::logger> logger_;
    // 配置的基准路径（例如 ./data/logs/sunkv.log），可能被策略扩展为按次/按日路径
    std::string file_path_;
    // 进程内固定的“实际写入路径”（避免 setConsoleEnabled 等触发 rebuild 时改变 per_run 的时间戳）
    std::string resolved_file_path_;
    std::string file_strategy_{"fixed"}; // fixed|per_run|daily
    bool console_enabled_{false};
};

// Log macros using spdlog
#define LOG_DEBUG Logger::instance().getLogger()->debug
#define LOG_INFO Logger::instance().getLogger()->info
#define LOG_WARN Logger::instance().getLogger()->warn
#define LOG_ERROR Logger::instance().getLogger()->error