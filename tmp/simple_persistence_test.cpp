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
        while (!reader.eof()) {
            auto entry = reader.read_next_entry();
            if (!entry) {
                continue;
            }
            
            recovered = true;
            
            switch (entry->operation) {
                case WALOperationType::SET:
                    storage.set(entry->key, entry->value, entry->ttl_ms);
                    std::cout << "恢复 SET: " << entry->key << " = " << entry->value << std::endl;
                    break;
                case WALOperationType::DEL:
                    storage.del(entry->key);
                    std::cout << "恢复 DEL: " << entry->key << std::endl;
                    break;
                case WALOperationType::CLEAR:
                    storage.clear();
                    std::cout << "恢复 CLEAR" << std::endl;
                    break;
                default:
                    break;
            }
        }
        
        reader.close();
        std::cout << "恢复结果: " << (recovered ? "成功" : "失败") << std::endl;
        
        if (recovered) {
            // 验证恢复的数据
            std::cout << "\n=== 恢复的数据验证 ===" << std::endl;
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
        std::cout << "=== 持久化写入测试 ===" << std::endl;
        
        StorageEngine& storage = StorageEngine::getInstance();
        
        WALWriter writer(wal_file);
        if (!writer.open()) {
            std::cout << "无法打开 WAL 文件进行写入" << std::endl;
            return 1;
        }
        
        std::cout << "写入数据到 WAL..." << std::endl;
        
        // 写入一些数据
        WALLogEntry entry1;
        entry1.operation = WALOperationType::SET;
        entry1.key = "key1";
        entry1.value = "value1";
        entry1.ttl_ms = -1;
        entry1.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.write_entry(entry1);
        
        WALLogEntry entry2;
        entry2.operation = WALOperationType::SET;
        entry2.key = "key2";
        entry2.value = "value2";
        entry2.ttl_ms = -1;
        entry2.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.write_entry(entry2);
        
        WALLogEntry entry3;
        entry3.operation = WALOperationType::SET;
        entry3.key = "key3";
        entry3.value = "value3";
        entry3.ttl_ms = -1;
        entry3.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.write_entry(entry3);
        
        // 写入一个要删除的键
        WALLogEntry entry4;
        entry4.operation = WALOperationType::SET;
        entry4.key = "deleted_key";
        entry4.value = "will_be_deleted";
        entry4.ttl_ms = -1;
        entry4.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.write_entry(entry4);
        
        // 删除操作
        WALLogEntry entry5;
        entry5.operation = WALOperationType::DEL;
        entry5.key = "deleted_key";
        entry5.ttl_ms = -1;
        entry5.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.write_entry(entry5);
        
        writer.flush();
        writer.close();
        
        std::cout << "数据写入完成，WAL 文件大小: " 
                  << std::filesystem::file_size(wal_file) << " 字节" << std::endl;
        
        std::cout << "\n现在请运行: ./simple_persistence_test recover" << std::endl;
        std::cout << "来测试数据恢复功能" << std::endl;
    }
    
    return 0;
}
