#include "../storage/WAL.h"
#include "../storage/StorageEngine.h"
#include <iostream>
#include <filesystem>

int main() {
    std::string wal_file = "simple_persistence.log";
    
    std::cout << "=== 快速恢复测试 ===" << std::endl;
    
    if (!std::filesystem::exists(wal_file)) {
        std::cout << "WAL 文件不存在: " << wal_file << std::endl;
        return 1;
    }
    
    std::cout << "WAL 文件大小: " << std::filesystem::file_size(wal_file) << " 字节" << std::endl;
    
    StorageEngine& storage = StorageEngine::getInstance();
    storage.clear();
    
    WALReader reader(wal_file);
    if (!reader.open()) {
        std::cout << "无法打开 WAL 文件" << std::endl;
        return 1;
    }
    
    std::cout << "开始读取 WAL 条目..." << std::endl;
    
    int count = 0;
    int max_entries = 10; // 限制读取数量，防止无限循环
    
    while (count < max_entries) {
        std::cout << "读取条目 " << count << "..." << std::endl;
        
        auto entry = reader.read_next_entry();
        if (!entry) {
            std::cout << "没有更多条目，停止读取" << std::endl;
            break;
        }
        
        std::cout << "条目 " << count << ": op=" << static_cast<int>(entry->operation) 
                  << ", key=" << entry->key << ", value=" << entry->value << std::endl;
        
        switch (entry->operation) {
            case WALOperationType::SET:
                storage.set(entry->key, entry->value, entry->ttl_ms);
                std::cout << "✅ 恢复 SET: " << entry->key << " = " << entry->value << std::endl;
                break;
            case WALOperationType::DEL:
                storage.del(entry->key);
                std::cout << "✅ 恢复 DEL: " << entry->key << std::endl;
                break;
            default:
                break;
        }
        
        count++;
    }
    
    reader.close();
    std::cout << "总共读取了 " << count << " 个条目" << std::endl;
    
    // 验证数据
    std::cout << "\n=== 数据验证 ===" << std::endl;
    try {
        std::string value1 = storage.get("key1");
        std::cout << "✅ key1 = " << value1 << std::endl;
    } catch (...) {
        std::cout << "❌ key1 未找到" << std::endl;
    }
    
    try {
        std::string value2 = storage.get("key2");
        std::cout << "✅ key2 = " << value2 << std::endl;
    } catch (...) {
        std::cout << "❌ key2 未找到" << std::endl;
    }
    
    try {
        std::string value3 = storage.get("key3");
        std::cout << "✅ key3 = " << value3 << std::endl;
    } catch (...) {
        std::cout << "❌ key3 未找到" << std::endl;
    }
    
    return 0;
}
