#include <iostream>
#include <memory>
#include <signal.h>
#include <filesystem>
#include "network/logger.h"
#include "Server.h"
#include "Config.h"

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
                    std::cerr << "SunKV is already running with PID " << existing_pid << std::endl;
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
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    std::cerr << "DEBUG: SunKV starting..." << std::endl;
    try {
        // 创建配置对象
        std::cerr << "DEBUG: Creating config..." << std::endl;
        auto config = std::make_unique<Config>();
        
        // 加载默认配置
        std::cerr << "DEBUG: Loading config..." << std::endl;
        std::string default_config = "./sunkv.conf";
        if (std::filesystem::exists(default_config)) {
            std::cerr << "DEBUG: Loading config file: " << default_config << std::endl;
            config->loadFromFile(default_config);
        }
        
        // 加载命令行参数（会覆盖配置文件中的设置）
        std::cerr << "DEBUG: Loading command line args..." << std::endl;
        config->loadFromCommandLine(argc, argv);
        
        // 设置日志级别
        std::cerr << "DEBUG: Setting log level..." << std::endl;
        Logger::instance().setLevelFromName(config->log_level);
        
        // 如果指定了日志文件，设置日志输出
        std::cerr << "DEBUG: Setting log file..." << std::endl;
        if (!config->log_file.empty()) {
            Logger::instance().setFile(config->log_file);
        }
        
        std::cerr << "DEBUG: Log setup complete..." << std::endl;
        std::cerr << "DEBUG: About to call LOG_INFO..." << std::endl;
        LOG_INFO("SunKV Server v1.0.0 starting...");
        std::cerr << "DEBUG: First LOG_INFO complete..." << std::endl;
        LOG_INFO("Configuration loaded successfully");
        std::cerr << "DEBUG: Second LOG_INFO complete..." << std::endl;
        
        // 守护进程模式处理
        std::cerr << "DEBUG: Checking daemon mode..." << std::endl;
        std::unique_ptr<PIDManager> pid_manager;
        if (config->daemon_mode) {
            std::cerr << "DEBUG: Daemon mode enabled..." << std::endl;
            pid_manager = std::make_unique<PIDManager>(config->pid_file);
            
            std::cerr << "DEBUG: Creating PID manager..." << std::endl;
            if (!pid_manager->create()) {
                std::cerr << "DEBUG: PID manager creation failed" << std::endl;
                return 1;
            }
            
            // 守护进程
            std::cerr << "DEBUG: Daemonizing..." << std::endl;
            if (daemon(0, 0) != 0) {
                std::cerr << "Failed to daemonize" << std::endl;
                return 1;
            }
            
            LOG_INFO("Running in daemon mode");
        } else {
            std::cerr << "DEBUG: Running in foreground mode" << std::endl;
        }
        
        // 创建服务器实例
        std::cerr << "DEBUG: Creating server instance..." << std::endl;
        auto server = std::make_unique<Server>(std::move(config));
        std::cerr << "DEBUG: Server instance created..." << std::endl;
        
        // 启动服务器
        std::cerr << "DEBUG: Starting server..." << std::endl;
        if (!server->start()) {
            std::cerr << "DEBUG: Server start failed" << std::endl;
            LOG_ERROR("Failed to start server");
            return 1;
        }
        std::cerr << "DEBUG: Server started successfully..." << std::endl;
        
        LOG_INFO("SunKV Server started successfully");
        LOG_INFO("Listening on {}:{}", 
                 config->bind_address, 
                 config->bind_port);
        
        // 等待服务器停止
        server->waitForStop();
        
        LOG_INFO("SunKV Server stopped gracefully");
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }
}