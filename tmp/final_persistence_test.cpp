#include "../storage/WAL.h"
#include "../storage/StorageEngine.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    std::string wal_file = "simple_persistence.log";
    
    if (argc == 2 && std::string(argv[1]) == "recover") {
        // 第二次运行：恢复数据
        std::cout << "=== 持久化恢复测试 ===" << std::endl;
        
        StorageEngine& storage = StorageEngine::getInstance();
        storage.clear();  // 清空内存
        
        if (!std::filesystem::exists(wal_file)) {
            std::cout << "WAL 文件不存在: " << wal_file << std::endl;
            return 1;
        }
        
        std::cout << "从 WAL 文件恢复数据..." << std::endl;
        
        WALReader reader(wal_file);
        if (!reader.open()) {
            std::cout << "无法打开 WAL 文件" << std::endl;
            return 1;
        }
        
        bool recovered = false;
        int count = 0;
        
        while (count < 10) {  // 限制循环次数
            auto entry = reader.read_next_entry();
            if (!entry) {
                std::cout << "没有更多条目，停止恢复" << std::endl;
                break;
            }
            
            recovered = true;
            count++;
            
            switch (entry->operation) {
                case WALOperationType::SET:
                    storage.set(entry->key, entry->value, entry->ttl_ms);
                    std::cout << "✅ 恢复 SET: " << entry->key << " = " << entry->value << std::endl;
                    break;
                case WALOperationType::DEL:
                    storage.del(entry->key);
                    std::cout << "✅ 恢复 DEL: " << entry->key << std::endl;
                    break;
                case WALOperationType::CLEAR:
                    storage.clear();
                    std::cout << "✅ 恢复 CLEAR" << std::endl;
                    break;
                default:
                    std::cout << "⚠️ 跳过操作: " << static_cast<int>(entry->operation) << std::endl;
                    break;
            }
        }
        
        reader.close();
        std::cout << "恢复结果: " << (recovered ? "成功" : "失败") << std::endl;
        std::cout << "总共恢复了 " << count << " 个条目" << std::endl;
        
        if (recovered) {
            // 验证恢复的数据
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
            
            // 检查被删除的键
            try {
                std::string value = storage.get("deleted_key");
                std::cout << "❌ deleted_key 仍然存在: " << value << std::endl;
            } catch (...) {
                std::cout << "✅ deleted_key 已被正确删除" << std::endl;
            }
        }
        
    } else {
        // 第一次运行：写入数据
        std::cout << "请先运行写入测试创建 WAL 文件" << std::endl;
        std::cout << "WAL 文件已存在: " << std::filesystem::exists(wal_file) << std::endl;
        std::cout << "文件大小: " << std::filesystem::file_size(wal_file) << " 字节" << std::endl;
    }
    
    return 0;
}
