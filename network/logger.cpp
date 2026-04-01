#include "logger.h"
#include <filesystem>
#include <spdlog/sinks/stdout_sinks.h>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    // 创建项目根目录下的 logs 目录
    std::string logs_dir = "../logs";
    if (!std::filesystem::exists(logs_dir)) {
        std::filesystem::create_directories(logs_dir);
    }
    
    // 使用 rotating_file_sink，每个文件最大 100MB，保留 3 个文件
    logger_ = spdlog::rotating_logger_mt("sunkv", "../logs/sunkv.log", 1024 * 1024 * 100, 3);
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
    if (level_name == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (level_name == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (level_name == "warn") {
        logger_->set_level(spdlog::level::warn);
    } else if (level_name == "error") {
        logger_->set_level(spdlog::level::err);
    } else {
        // 默认使用 info 级别
        logger_->set_level(spdlog::level::info);
    }
}

void Logger::setFile(const std::string& filename) {
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
    
    logger_->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");
    logger_->flush_on(spdlog::level::info);
}