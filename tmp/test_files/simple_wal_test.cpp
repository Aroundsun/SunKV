#include "storage/WAL.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string test_file = "/tmp/simple_wal_test.log";
    std::filesystem::remove(test_file);
    
    std::cout << "=== Simple WAL Test ===" << std::endl;
    
    // 创建 WALWriter
    WALWriter writer(test_file);
    
    // 打开文件
    bool opened = writer.open();
    std::cout << "Open result: " << (opened ? "success" : "failed") << std::endl;
    
    if (opened) {
        // 创建一个简单的条目
        WALLogEntry entry;
        entry.operation = WALOperationType::SET;
        entry.key = "test_key";
        entry.value = "test_value";
        entry.ttl_ms = -1;
        entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        // 写入条目
        bool written = writer.write_entry(entry);
        std::cout << "Write result: " << (written ? "success" : "failed") << std::endl;
        
        // 检查文件是否存在
        bool exists = std::filesystem::exists(test_file);
        std::cout << "File exists: " << (exists ? "yes" : "no") << std::endl;
        
        if (exists) {
            auto size = std::filesystem::file_size(test_file);
            std::cout << "File size: " << size << " bytes" << std::endl;
            
            // 显示文件内容
            std::ifstream file(test_file, std::ios::binary);
            if (file.is_open()) {
                char buffer[64];
                file.read(buffer, 64);
                std::cout << "File content: ";
                for (int i = 0; i < file.gcount(); ++i) {
                    printf("%02x ", (unsigned char)buffer[i]);
                }
                std::cout << std::endl;
                file.close();
            }
        }
        
        // 获取统计信息
        auto stats = writer.get_stats();
        std::cout << "Stats: " << stats.total_entries << " entries, " 
                  << stats.total_bytes << " bytes" << std::endl;
    }
    
    return 0;
}
