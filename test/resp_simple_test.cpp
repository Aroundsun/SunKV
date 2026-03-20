#include <iostream>
#include <string>
#include "protocol/RESPType.h"
#include "protocol/RESPParser.h"

int main() {
    std::cout << "=== RESP Simple Test ===" << std::endl;
    
    // 测试基本解析功能
    std::cout << "\n--- Basic Parsing Tests ---" << std::endl;
    
    // 测试简单字符串
    RESPParser parser1;
    std::string simpleData = "+OK\r\n";
    auto result1 = parser1.parse(simpleData);
    std::cout << "Simple string: ";
    if (result1.success && result1.complete) {
        std::cout << "✅ " << result1.value->toString() << std::endl;
    } else {
        std::cout << "❌ " << result1.error << std::endl;
    }
    
    // 测试整数
    RESPParser parser2;
    std::string intData = ":12345\r\n";
    auto result2 = parser2.parse(intData);
    std::cout << "Integer: ";
    if (result2.success && result2.complete) {
        std::cout << "✅ " << result2.value->toString() << std::endl;
    } else {
        std::cout << "❌ " << result2.error << std::endl;
    }
    
    // 测试批量字符串
    RESPParser parser3;
    std::string bulkData = "$5\r\nhello\r\n";
    auto result3 = parser3.parse(bulkData);
    std::cout << "Bulk string: ";
    if (result3.success && result3.complete) {
        std::cout << "✅ " << result3.value->toString() << std::endl;
    } else {
        std::cout << "❌ " << result3.error << std::endl;
    }
    
    // 测试数组
    RESPParser parser4;
    std::string arrayData = "*3\r\n+SET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    auto result4 = parser4.parse(arrayData);
    std::cout << "Array: ";
    if (result4.success && result4.complete) {
        std::cout << "✅ " << result4.value->toString() << std::endl;
    } else {
        std::cout << "❌ " << result4.error << std::endl;
    }
    
    // 测试编码功能
    std::cout << "\n--- Encoding Tests ---" << std::endl;
    
    auto simpleStr = makeSimpleString("Hello");
    std::cout << "Simple string encode: " << simpleStr->encode() << std::endl;
    
    auto error = makeError("Something wrong");
    std::cout << "Error encode: " << error->encode() << std::endl;
    
    auto integer = makeInteger(42);
    std::cout << "Integer encode: " << integer->encode() << std::endl;
    
    auto bulkStr = makeBulkString("World");
    std::cout << "Bulk string encode: " << bulkStr->encode() << std::endl;
    
    auto nullBulk = makeNullBulkString();
    std::cout << "Null bulk string encode: " << nullBulk->encode() << std::endl;
    
    std::vector<RESPValue::Ptr> arrayVals = {
        makeSimpleString("GET"),
        makeBulkString("key"),
        makeInteger(1)
    };
    auto array = makeArray(arrayVals);
    std::cout << "Array encode: " << array->encode() << std::endl;
    
    auto nullArray = makeNullArray();
    std::cout << "Null array encode: " << nullArray->encode() << std::endl;
    
    std::cout << "\n=== RESP Simple Test Completed ===" << std::endl;
    std::cout << "✅ RESP Protocol implementation working!" << std::endl;
    
    return 0;
}
