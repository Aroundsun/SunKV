#include "logger.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <spdlog/sinks/stdout_color_sinks.h>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    // 默认不启用文件输出：由配置/CLI 决定 setFile() 后再落盘，
    // 避免在配置尚未接入前就创建默认 sunkv.log（尤其是 per_run/daily 策略下会产生“多余的 sunkv.log”）。
    file_path_.clear();
    resolved_file_path_.clear();
    console_enabled_ = false; // 由配置决定；main 会调用 setConsoleEnabled()
    rebuildLogger();
}

void Logger::setLevel(spdlog::level::level_enum level) {
    logger_->set_level(level);
    logger_->flush_on(level);
}

spdlog::level::level_enum Logger::getLevel() const {
    return logger_->level();
}

std::shared_ptr<spdlog::logger> Logger::getLogger() const {
    return logger_;
}

void Logger::setLevelFromName(const std::string& level_name) {
    std::string name = level_name;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    spdlog::level::level_enum lv = spdlog::level::info;
    if (name == "DEBUG") {
        lv = spdlog::level::debug;
    } else if (name == "INFO") {
        lv = spdlog::level::info;
    } else if (name == "WARN" || name == "WARNING") {
        lv = spdlog::level::warn;
    } else if (name == "ERROR" || name == "ERR") {
        lv = spdlog::level::err;
    }
    logger_->set_level(lv);
    // 与当前级别对齐：DEBUG 时也要及时刷盘，否则文件里看不到运行期 debug 行
    logger_->flush_on(lv);
}

void Logger::setFile(const std::string& filename) {
    file_path_ = filename;
    resolved_file_path_.clear();
    rebuildLogger();
}

void Logger::setConsoleEnabled(bool enabled) {
    console_enabled_ = enabled;
    rebuildLogger();
}

void Logger::setFileStrategy(const std::string& strategy_name) {
    std::string s = strategy_name;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s == "fixed" || s == "per_run" || s == "daily") {
        file_strategy_ = s;
    } else {
        file_strategy_ = "fixed";
    }
    resolved_file_path_.clear();
    rebuildLogger();
}

std::string Logger::resolveFilePath() const {
    if (file_path_.empty()) {
        return file_path_;
    }
    if (file_strategy_ == "fixed") {
        return file_path_;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::filesystem::path p(file_path_);
    std::filesystem::path parent = p.parent_path();
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    std::ostringstream suffix;
    if (file_strategy_ == "per_run") {
        suffix << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    } else { // daily
        suffix << std::put_time(&tm_buf, "%Y%m%d");
    }

    std::filesystem::path out = parent / (stem + "_" + suffix.str() + ext);
    return out.string();
}

void Logger::rebuildLogger() {
    // 保留当前级别
    spdlog::level::level_enum current_level = spdlog::level::info;
    if (logger_) {
        current_level = logger_->level();
    }

    // drop 旧 logger（避免同名重复注册）
    spdlog::drop("sunkv");

    std::vector<spdlog::sink_ptr> sinks;

    // file sink（如果 file_path_ 为空则不创建文件输出）
    if (!file_path_.empty()) {
        if (resolved_file_path_.empty()) {
            resolved_file_path_ = resolveFilePath();
        }
        std::filesystem::path file_path(resolved_file_path_);
        std::filesystem::path dir_path = file_path.parent_path();
        if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
            std::filesystem::create_directories(dir_path);
        }

        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            resolved_file_path_, 1024 * 1024 * 100, 3));
    }

    // console sink
    if (console_enabled_) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    // 若两个都没启用，兜底到控制台（避免 logger_ 为空）
    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    logger_ = std::make_shared<spdlog::logger>("sunkv", sinks.begin(), sinks.end());
    spdlog::register_logger(logger_);

    logger_->set_level(current_level);
    logger_->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] [%t] %v");
    logger_->flush_on(current_level);
}