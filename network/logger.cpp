#include "logger.h"
#include <filesystem>

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