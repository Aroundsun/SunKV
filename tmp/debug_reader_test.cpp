#include "../storage/WAL.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string test_file = "../tmp/test_wal_reader.log";
    
    std::cout << "=== WAL Reader 测试调试 ===" << std::endl;
    std::cout << "测试文件: " << test_file << std::endl;
    
    // 清理旧文件
    std::filesystem::remove(test_file);
    
    // 写入数据
    {
        std::cout << "创建 WALWriter..." << std::endl;
        WALWriter writer(test_file);
        bool opened = writer.open();
        std::cout << "Writer 打开结果: " << (opened ? "成功" : "失败") << std::endl;
        
        if (opened) {
            for (int i = 0; i < 10; ++i) {
                WALLogEntry entry;
                entry.operation = (i % 3 == 0) ? WALOperationType::DEL : WALOperationType::SET;
                entry.key = "key" + std::to_string(i);
                entry.value = "value" + std::to_string(i);
                entry.ttl_ms = i * 1000;
                entry.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                
                bool written = writer.write_entry(entry);
                std::cout << "写入条目 " << i << ": " << (written ? "成功" : "失败") << std::endl;
            }
            
            writer.flush();
            writer.close();
            
            if (std::filesystem::exists(test_file)) {
                std::cout << "文件创建成功，大小: " << std::filesystem::file_size(test_file) << " 字节" << std::endl;
            } else {
                std::cout << "文件创建失败！" << std::endl;
            }
        }
    }
    
    // 读取数据
    {
        std::cout << "\n创建 WALReader..." << std::endl;
        WALReader reader(test_file);
        bool opened = reader.open();
        std::cout << "Reader 打开结果: " << (opened ? "成功" : "失败") << std::endl;
        
        if (opened) {
            int entry_count = 0;
            while (!reader.eof()) {
                auto entry = reader.read_next_entry();
                if (!entry) {
                    std::cout << "没有更多条目，停止读取" << std::endl;
                    break;
                }
                
                entry_count++;
                std::cout << "读取条目 " << entry_count << ": " << entry->key << " = " << entry->value << std::endl;
                
                if (entry_count >= 15) {  // 防止无限循环
                    std::cout << "达到最大读取次数，停止" << std::endl;
                    break;
                }
            }
            
            auto stats = reader.get_stats();
            std::cout << "统计: " << stats.valid_entries << " 有效, " << stats.invalid_entries << " 无效" << std::endl;
            
            reader.close();
        }
    }
    
    return 0;
}
