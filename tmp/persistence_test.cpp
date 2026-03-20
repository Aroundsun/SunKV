#include "../storage/WAL.h"
#include "../storage/StorageEngine.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    std::string wal_file = "persistence_test.log";
    std::string db_file = "persistence_test.db";
    
    if (argc == 2 && std::string(argv[1]) == "recover") {
        // 第二次运行：恢复数据
        std::cout << "=== 持久化恢复测试 ===" << std::endl;
        
        StorageEngine& storage = StorageEngine::getInstance();
        storage.clear();  // 清空内存
        
        WALManager wal_manager("./wal");
        if (!wal_manager.initialize()) {
            std::cout << "Failed to initialize WAL manager" << std::endl;
            return 1;
        }
        
        std::cout << "从 WAL 文件恢复数据..." << std::endl;
        bool recovered = wal_manager.replay(storage);
        std::cout << "恢复结果: " << (recovered ? "成功" : "失败") << std::endl;
        
        if (recovered) {
            // 验证恢复的数据
            std::cout << "\n=== 恢复的数据 ===" << std::endl;
            try {
                std::string value1 = storage.get("key1");
                std::cout << "key1 = " << value1 << std::endl;
            } catch (...) {
                std::cout << "key1 未找到" << std::endl;
            }
            
            try {
                std::string value2 = storage.get("key2");
                std::cout << "key2 = " << value2 << std::endl;
            } catch (...) {
                std::cout << "key2 未找到" << std::endl;
            }
            
            try {
                std::string value3 = storage.get("key3");
                std::cout << "key3 = " << value3 << std::endl;
            } catch (...) {
                std::cout << "key3 未找到" << std::endl;
            }
            
            // 检查被删除的键
            try {
                std::string value = storage.get("deleted_key");
                std::cout << "deleted_key 仍然存在: " << value << std::endl;
            } catch (...) {
                std::cout << "deleted_key 已被正确删除 ✅" << std::endl;
            }
        }
        
    } else {
        // 第一次运行：写入数据
        std::cout << "=== 持久化写入测试 ===" << std::endl;
        
        // 清理旧文件
        std::filesystem::remove(wal_file);
        std::filesystem::remove(db_file);
        
        StorageEngine& storage = StorageEngine::getInstance();
        
        WALManager wal_manager("./wal");
        if (!wal_manager.initialize()) {
            std::cout << "Failed to initialize WAL manager" << std::endl;
            return 1;
        }
        
        std::cout << "写入数据到 WAL..." << std::endl;
        
        // 写入一些数据
        wal_manager.write_set("key1", "value1");
        wal_manager.write_set("key2", "value2");
        wal_manager.write_set("key3", "value3");
        wal_manager.write_set("deleted_key", "will_be_deleted");
        
        // 删除一个键
        wal_manager.write_del("deleted_key");
        
        // 事务测试
        wal_manager.begin_transaction();
        wal_manager.write_set("tx_key1", "tx_value1");
        wal_manager.write_set("tx_key2", "tx_value2");
        wal_manager.commit_transaction();
        
        // 刷新 WAL
        wal_manager.flush();
        
        std::cout << "数据写入完成，WAL 文件大小: " 
                  << std::filesystem::file_size(wal_file) << " 字节" << std::endl;
        
        std::cout << "\n现在请运行: ./persistence_test recover" << std::endl;
        std::cout << "来测试数据恢复功能" << std::endl;
    }
    
    return 0;
}
