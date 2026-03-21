#include "../storage/Snapshot.h"
#include <iostream>
#include <cassert>
#include <filesystem>

int main() {
    std::cout << "=== Snapshot Simple Test ===" << std::endl;
    
    try {
        // 测试基本的写入和读取
        std::string test_file = "simple_test.snap";
        std::filesystem::remove(test_file);
        
        // 写入测试
        {
            SnapshotWriter writer(test_file);
            if (!writer.open()) {
                std::cout << "❌ Failed to open writer" << std::endl;
                return 1;
            }
            
            if (!writer.write_data("test_key", "test_value", -1)) {
                std::cout << "❌ Failed to write data" << std::endl;
                return 1;
            }
            
            writer.close();
        }
        
        // 检查文件是否存在
        if (!std::filesystem::exists(test_file)) {
            std::cout << "❌ File was not created" << std::endl;
            return 1;
        }
        
        auto file_size = std::filesystem::file_size(test_file);
        std::cout << "✅ File created: " << file_size << " bytes" << std::endl;
        
        // 读取测试
        {
            SnapshotReader reader(test_file);
            if (!reader.open()) {
                std::cout << "❌ Failed to open reader" << std::endl;
                return 1;
            }
            
            auto entry = reader.read_next_entry();
            if (!entry) {
                std::cout << "❌ Failed to read entry" << std::endl;
                return 1;
            }
            
            if (entry->key != "test_key" || entry->value != "test_value") {
                std::cout << "❌ Data mismatch: " << entry->key << " = " << entry->value << std::endl;
                return 1;
            }
            
            auto stats = reader.get_stats();
            std::cout << "✅ Read " << stats.valid_entries << " entries successfully" << std::endl;
        }
        
        // 清理
        std::filesystem::remove(test_file);
        
        std::cout << "\n🎉 SNAPSHOT TEST PASSED! 🎉" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
