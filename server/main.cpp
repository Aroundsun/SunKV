#include <iostream>
#include <memory>
#include <signal.h>
#include <filesystem>
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
                    LOG_ERROR("SunKV 已在运行，PID: {}", existing_pid);
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
    try {
        // 提前初始化 Logger，避免启动早期错误落到 stdout/stderr
        (void)Logger::instance();

        // 创建配置对象
        auto& config = Config::getInstance();
        
        // 加载默认配置
        std::string default_config = "./sunkv.conf";
        if (std::filesystem::exists(default_config)) {
            if (!config.loadFromFile(default_config)) {
                LOG_ERROR("加载默认配置失败: {}", default_config);
                return 1;
            }
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

        // 是否输出到控制台（文件 + 控制台双 sink）
        Logger::instance().setConsoleEnabled(config.enable_console_log);

        // 日志文件策略（fixed/per_run/daily）
        Logger::instance().setFileStrategy(config.log_strategy);
        
        // 如果指定了日志文件，设置日志输出
        if (!config.log_file.empty()) {
            Logger::instance().setFile(config.log_file);
            LOG_INFO("日志文件: {}", config.log_file);
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
                LOG_ERROR("守护进程化失败");
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
            LOG_ERROR("优雅关闭过程中发生异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("优雅关闭过程中发生未知异常");
        }
        
        LOG_INFO("SunKV Server 已优雅停止");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("致命错误: {}", e.what());
        return 1;
    } catch (...) {
        LOG_ERROR("未知致命错误");
        return 1;
    }
}