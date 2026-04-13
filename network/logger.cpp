#include "logger.h"
#include <filesystem>
#include <iostream>
#include <spdlog/sinks/stdout_sinks.h>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    // 默认将日志放在 data/logs 目录
    std::string logs_dir = "./data/logs";
    if (!std::filesystem::exists(logs_dir)) {
        std::filesystem::create_directories(logs_dir);
    }
    
    // 使用 rotating_file_sink，每个文件最大 100MB，保留 3 个文件
    logger_ = spdlog::rotating_logger_mt("sunkv", "./data/logs/sunkv.log", 1024 * 1024 * 100, 3);
    logger_->set_level(spdlog::level::info);
    logger_->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");
    logger_->flush_on(spdlog::level::info);
}

void Logger::setLevel(spdlog::level::level_enum level) {
    logger_->set_level(level);
}

spdlog::level::level_enum Logger::getLevel() const {
    return logger_->level();
}

std::shared_ptr<spdlog::logger> Logger::getLogger() const {
    return logger_;
}

void Logger::setLevelFromName(const std::string& level_name) {
    spdlog::level::level_enum lv = spdlog::level::info;
    if (level_name == "DEBUG") {
        lv = spdlog::level::debug;
    } else if (level_name == "INFO") {
        lv = spdlog::level::info;
    } else if (level_name == "WARN") {
        lv = spdlog::level::warn;
    } else if (level_name == "ERROR") {
        lv = spdlog::level::err;
    }
    logger_->set_level(lv);
    // 与当前级别对齐：DEBUG 时也要及时刷盘，否则文件里看不到运行期 debug 行
    logger_->flush_on(lv);
}

void Logger::setFile(const std::string& filename) {
    // 先保存当前日志级别
    auto current_level = logger_->level();
    
    // 先删除现有的 logger
    spdlog::drop("sunkv");
    
    if (filename.empty()) {
        // 如果文件名为空，使用控制台输出
        logger_ = spdlog::create<spdlog::sinks::stdout_sink_mt>("sunkv");
    } else {
        // 创建目录
        std::filesystem::path file_path(filename);
        std::filesystem::path dir_path = file_path.parent_path();
        if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
            std::filesystem::create_directories(dir_path);
        }
        
        // 使用文件输出
        logger_ = spdlog::rotating_logger_mt("sunkv", filename, 1024 * 1024 * 100, 3);
    }
    
    logger_->set_level(current_level);  // 恢复之前的日志级别
    logger_->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");
    logger_->flush_on(current_level);
}