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
    
private:
    Logger();
    void rebuildLogger();

    std::shared_ptr<spdlog::logger> logger_;
    std::string file_path_;
    bool console_enabled_{false};
};

// Log macros using spdlog
#define LOG_DEBUG Logger::instance().getLogger()->debug
#define LOG_INFO Logger::instance().getLogger()->info
#define LOG_WARN Logger::instance().getLogger()->warn
#define LOG_ERROR Logger::instance().getLogger()->error