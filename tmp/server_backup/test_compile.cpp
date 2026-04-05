#include <iostream>
#include "../network/logger.h"
#include "../protocol/RESPType.h"
#include "../command/Command.h"

int main() {
    std::cout << "Testing SunKV Server compilation..." << std::endl;
    
    // 测试日志系统
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("SunKV Server compilation test started");
    
    // 测试 RESP 类型
    auto simple = makeSimpleString("OK");
    auto error = makeError("Test error");
    auto bulk = makeBulkString("Hello World");
    auto integer = makeInteger(42);
    
    std::cout << "✅ RESP types created successfully" << std::endl;
    
    // 测试 CommandResult
    CommandResult success(true, simple);
    CommandResult fail(false, "Test failed");
    
    std::cout << "✅ CommandResult created successfully" << std::endl;
    
    LOG_INFO("All tests passed!");
    std::cout << "🎉 SunKV Server compilation test successful! 🎉" << std::endl;
    
    return 0;
}
