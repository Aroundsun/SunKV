#include <iostream>
#include <memory>
#include <signal.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "network/logger.h"
#include "Server.h"
#include "../common/Config.h"

/**
 * @brief 守护进程模式下的 PID 文件管理
 */
class PIDManager {
public:
    explicit PIDManager(const std::string& pid_file) : pid_file_(pid_file) {}
    
    ~PIDManager() {
        remove();
    }
    
    bool create() {
        // 检查是否已有进程在运行
        if (std::filesystem::exists(pid_file_)) {
            std::ifstream file(pid_file_);
            if (file.is_open()) {
                pid_t existing_pid;
                file >> existing_pid;
                file.close();
                
                // 检查进程是否仍在运行
                if (kill(existing_pid, 0) == 0) {
                    std::cerr << "SunKV 已在运行，PID: " << existing_pid << std::endl;
                    return false;
                }
            }
        }
        
        // 写入当前进程 PID
        std::ofstream file(pid_file_);
        if (file.is_open()) {
            file << getpid() << std::endl;
            file.close();
            return true;
        }
        
        return false;
    }
    
    void remove() {
        if (std::filesystem::exists(pid_file_)) {
            std::filesystem::remove(pid_file_);
        }
    }

private:
    std::string pid_file_;
};

/**
 * @brief 基于模板日志路径生成本次运行的独立日志文件
 *
 * 例如：./data/logs/sunkv.log -> ./data/logs/sunkv_20260408_211500.log
 */
static std::string buildRunLogFilePath(const std::string& base_log_file) {
    if (base_log_file.empty()) {
        return base_log_file;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::ostringstream ts;
    ts << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");

    std::filesystem::path p(base_log_file);
    std::filesystem::path parent = p.parent_path();
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();

    std::filesystem::path run_file = parent / (stem + "_" + ts.str() + ext);
    return run_file.string();
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    try {
        // 创建配置对象
        auto& config = Config::getInstance();
        
        // 加载默认配置
        std::string default_config = "./sunkv.conf";
        if (std::filesystem::exists(default_config)) {
            config.loadFromFile(default_config);
        }
        
        // 加载命令行参数（会覆盖配置文件中的设置）
        config.loadFromArgs(argc, argv);

#ifndef NDEBUG
        // Debug 构建：未显式指定 --log-level 时默认 DEBUG，便于运行期在日志文件中看到连接/消息等
        if (!config.log_level_from_cli) {
            config.log_level = "DEBUG";
        }
#endif
        
        // 设置日志级别
        Logger::instance().setLevelFromName(config.log_level);
        
        // 如果指定了日志文件，设置日志输出
        if (!config.log_file.empty()) {
            std::string run_log_file = buildRunLogFilePath(config.log_file);
            Logger::instance().setFile(run_log_file);
            std::cout << "本次运行日志文件: " << run_log_file << std::endl;
        }
        
        LOG_INFO("SunKV Server v1.0.0 正在启动...");
        LOG_INFO("配置加载成功");
        
        // 守护进程模式处理
        std::unique_ptr<PIDManager> pid_manager;
        if (false) {  // 暂时禁用守护进程模式
            pid_manager = std::make_unique<PIDManager>("./sunkv.pid");
            
            if (!pid_manager->create()) {
                return 1;
            }
            
            // 守护进程
            if (daemon(0, 0) != 0) {
                std::cerr << "守护进程化失败" << std::endl;
                return 1;
            }
            
            LOG_INFO("当前以守护进程模式运行");
        } else {
        }
        
        // 创建服务器实例
        auto server = std::make_unique<Server>(config);
        
        // 启动服务器
        if (!server->start()) {
            LOG_ERROR("服务器启动失败");
            return 1;
        }
        
        LOG_INFO("SunKV Server 启动成功");
        LOG_INFO("监听地址 {}:{}", 
                 config.host, 
                 config.port);
        
        // 等待服务器停止 - 使用新的主循环逻辑
        while (server->isRunning() && !server->isStopping()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 主循环退出后执行优雅关闭
        LOG_DEBUG("主循环已退出, running={}, stopping={}",
                  server->isRunning(), server->isStopping());
        
        try {
            if (server->isStopping()) {
                LOG_DEBUG("即将执行优雅关闭...");
                LOG_INFO("正在执行优雅关闭...");
                server->stop();
                LOG_DEBUG("优雅关闭完成");
            } else {
                LOG_INFO("服务器停止（未经过优雅关闭流程）");
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR: 优雅关闭过程中发生异常: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ERROR: 优雅关闭过程中发生未知异常" << std::endl;
        }
        
        LOG_INFO("SunKV Server 已优雅停止");
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未知致命错误" << std::endl;
        return 1;
    }
}