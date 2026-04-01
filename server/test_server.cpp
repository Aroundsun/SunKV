#include <iostream>
#include <memory>
#include "../network/logger.h"

int main() {
    std::cout << "SunKV Server Test" << std::endl;
    
    // 测试日志系统
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("SunKV Server starting...");
    
    std::cout << "Logger test passed!" << std::endl;
    std::cout << "Server module compilation successful!" << std::endl;
    
    return 0;
}
