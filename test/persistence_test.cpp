#include "../storage/WAL.h"
#include "../storage/Snapshot.h"
#include "../storage/Recovery.h"
#include "../storage/StorageEngine.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <map>
#include <chrono>
#include <thread>
#include <random>

class PersistenceTester {
public:
    static void runAllTests() {
        std::cout << "=== Persistence Comprehensive Test Suite ===" << std::endl;
        
        testBasicPersistence();
        testCrashRecovery();
        testDataConsistency();
        testPerformance();
        
        std::cout << "\n🎉 ALL PERSISTENCE TESTS PASSED! 🎉" << std::endl;
        std::cout << "Persistence functionality is working!" << std::endl;
    }
    
private:
    static void testBasicPersistence() {
        std::cout << "\n--- Basic Persistence Test ---" << std::endl;
        
        std::string test_dir = "basic_persistence_test";
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        
        try {
            // 测试 WAL 基本功能
            std::string wal_file = test_dir + "/test.log";
            {
                // 先创建空文件
                std::ofstream empty_file(wal_file);
                empty_file.close();
                
                WALWriter writer(wal_file);
                assert(writer.open());
                
                // 写入测试数据
                assert(writer.write_put("key1", "value1"));
                assert(writer.write_put("key2", "value2"));
                assert(writer.write_delete("key1"));
                assert(writer.write_put("key3", "value3"));
                
                writer.close();
            }
            
            // 验证 WAL 文件
            assert(std::filesystem::exists(wal_file));
            auto wal_size = std::filesystem::file_size(wal_file);
            assert(wal_size > 0);
            
            // 测试 WAL 读取
            {
                WALReader reader(wal_file);
                assert(reader.open());
                
                auto entry1 = reader.read_next_entry();
                assert(entry1 && entry1->operation == WALOperationType::SET);
                assert(entry1->key == "key1" && entry1->value == "value1");
                
                auto entry2 = reader.read_next_entry();
                assert(entry2 && entry2->operation == WALOperationType::SET);
                assert(entry2->key == "key2" && entry2->value == "value2");
                
                auto entry3 = reader.read_next_entry();
                assert(entry3 && entry3->operation == WALOperationType::DEL);
                assert(entry3->key == "key1");
                
                auto entry4 = reader.read_next_entry();
                assert(entry4 && entry4->operation == WALOperationType::SET);
                assert(entry4->key == "key3" && entry4->value == "value3");
                
                reader.close();
            }
            
            // 测试快照基本功能
            std::string snapshot_file = test_dir + "/test.snap";
            {
                // 先创建空文件
                std::ofstream empty_file(snapshot_file);
                empty_file.close();
                
                SnapshotWriter writer(snapshot_file);
                assert(writer.open());
                
                assert(writer.write_data("snap_key1", "snap_value1", -1));
                assert(writer.write_data("snap_key2", "snap_value2", 3600000));
                assert(writer.write_deleted("deleted_key"));
                
                writer.close();
            }
            
            // 验证快照文件
            assert(std::filesystem::exists(snapshot_file));
            auto snap_size = std::filesystem::file_size(snapshot_file);
            assert(snap_size > 0);
            
            // 测试快照读取
            {
                SnapshotReader reader(snapshot_file);
                assert(reader.open());
                
                auto snap_entry1 = reader.read_next_entry();
                assert(snap_entry1 && snap_entry1->type == SnapshotEntryType::DATA);
                assert(snap_entry1->key == "snap_key1" && snap_entry1->value == "snap_value1");
                
                auto snap_entry2 = reader.read_next_entry();
                assert(snap_entry2 && snap_entry2->type == SnapshotEntryType::DATA);
                assert(snap_entry2->key == "snap_key2" && snap_entry2->value == "snap_value2");
                
                auto snap_entry3 = reader.read_next_entry();
                assert(snap_entry3 && snap_entry3->type == SnapshotEntryType::DELETED);
                assert(snap_entry3->key == "deleted_key");
                
                reader.close();
            }
            
            std::cout << "✅ Basic persistence test passed" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Basic persistence test failed: " << e.what() << std::endl;
            throw;
        }
        
        // 清理
        std::filesystem::remove_all(test_dir);
    }
    
    static void testCrashRecovery() {
        std::cout << "\n--- Crash Recovery Test ---" << std::endl;
        
        std::string test_dir = "crash_recovery_test";
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        
        try {
            // 模拟数据写入过程
            std::string snapshot_dir = test_dir + "/snapshots";
            std::string wal_dir = test_dir + "/wal";
            std::filesystem::create_directories(snapshot_dir);
            std::filesystem::create_directories(wal_dir);
            
            // 创建初始快照
            std::map<std::string, std::string> initial_data;
            initial_data["key1"] = "value1";
            initial_data["key2"] = "value2";
            initial_data["key3"] = "value3";
            
            {
                SnapshotManager manager(snapshot_dir);
                assert(manager.initialize());
                assert(manager.create_snapshot(initial_data));
            }
            
            // 写入一些 WAL 操作
            std::string wal_file = wal_dir + "/crash.log";
            {
                WALWriter writer(wal_file);
                assert(writer.open());
                
                assert(writer.write_put("key4", "value4"));
                assert(writer.write_put("key5", "value5"));
                assert(writer.write_delete("key2"));
                
                // 模拟"崩溃" - 不正常关闭
                // writer.close(); // 故意不调用 close
            }
            
            // 模拟重启后的恢复
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> recovered_data;
            
            auto status = recovery.recover_data(recovered_data);
            assert(status == RecoveryStatus::SUCCESS);
            
            // 验证恢复的数据
            assert(recovered_data.size() == 4); // key1, key3, key4, key5
            assert(recovered_data["key1"] == "value1");
            assert(recovered_data.find("key2") == recovered_data.end()); // 被删除
            assert(recovered_data["key3"] == "value3");
            assert(recovered_data["key4"] == "value4");
            assert(recovered_data["key5"] == "value5");
            
            auto stats = recovery.get_stats();
            assert(stats.snapshot_entries_loaded == 3);
            assert(stats.wal_entries_replayed == 3);
            
            std::cout << "✅ Crash recovery test passed: " 
                      << recovered_data.size() << " keys recovered" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Crash recovery test failed: " << e.what() << std::endl;
            throw;
        }
        
        // 清理
        std::filesystem::remove_all(test_dir);
    }
    
    static void testDataConsistency() {
        std::cout << "\n--- Data Consistency Test ---" << std::endl;
        
        std::string test_dir = "consistency_test";
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        
        try {
            std::string snapshot_dir = test_dir + "/snapshots";
            std::string wal_dir = test_dir + "/wal";
            std::filesystem::create_directories(snapshot_dir);
            std::filesystem::create_directories(wal_dir);
            
            // 创建包含各种操作的测试数据
            std::string wal_file = wal_dir + "/consistency.log";
            std::string snapshot_file = snapshot_dir + "/consistency.snap";
            
            // 写入快照
            {
                SnapshotWriter writer(snapshot_file);
                assert(writer.open());
                
                for (int i = 0; i < 100; ++i) {
                    std::string key = "key" + std::to_string(i);
                    std::string value = "value" + std::to_string(i);
                    assert(writer.write_data(key, value, -1));
                }
                
                writer.close();
            }
            
            // 写入 WAL 操作
            {
                WALWriter writer(wal_file);
                assert(writer.open());
                
                // 修改一些数据
                for (int i = 0; i < 50; ++i) {
                    std::string key = "key" + std::to_string(i);
                    std::string value = "modified_value" + std::to_string(i);
                    assert(writer.write_put(key, value));
                }
                
                // 删除一些数据
                for (int i = 75; i < 100; ++i) {
                    std::string key = "key" + std::to_string(i);
                    assert(writer.write_delete(key));
                }
                
                writer.close();
            }
            
            // 恢复数据
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            options.enable_consistency_check = true;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> recovered_data;
            
            auto status = recovery.recover_data(recovered_data);
            assert(status == RecoveryStatus::SUCCESS);
            
            // 验证数据一致性
            assert(recovered_data.size() == 75); // 100 - 25 (deleted)
            
            // 验证修改的数据
            for (int i = 0; i < 50; ++i) {
                std::string key = "key" + std::to_string(i);
                std::string expected_value = "modified_value" + std::to_string(i);
                assert(recovered_data[key] == expected_value);
            }
            
            // 验证未修改的数据
            for (int i = 50; i < 75; ++i) {
                std::string key = "key" + std::to_string(i);
                std::string expected_value = "value" + std::to_string(i);
                assert(recovered_data[key] == expected_value);
            }
            
            // 验证被删除的数据
            for (int i = 75; i < 100; ++i) {
                std::string key = "key" + std::to_string(i);
                assert(recovered_data.find(key) == recovered_data.end());
            }
            
            // 验证一致性检查
            bool is_consistent = recovery.verify_consistency(recovered_data);
            assert(is_consistent);
            
            auto stats = recovery.get_stats();
            assert(stats.consistency_errors == 0);
            
            std::cout << "✅ Data consistency test passed: " 
                      << recovered_data.size() << " keys, consistent" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Data consistency test failed: " << e.what() << std::endl;
            throw;
        }
        
        // 清理
        std::filesystem::remove_all(test_dir);
    }
    
    static void testPerformance() {
        std::cout << "\n--- Performance Test ---" << std::endl;
        
        std::string test_dir = "performance_test";
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        
        try {
            const int NUM_OPERATIONS = 10000;
            const int BATCH_SIZE = 100;
            
            // 测试 WAL 写入性能
            auto start_time = std::chrono::high_resolution_clock::now();
            
            std::string wal_file = test_dir + "/perf.log";
            {
                WALWriter writer(wal_file);
                assert(writer.open());
                
                for (int i = 0; i < NUM_OPERATIONS; ++i) {
                    std::string key = "perf_key_" + std::to_string(i);
                    std::string value = "perf_value_" + std::to_string(i);
                    assert(writer.write_put(key, value));
                    
                    if (i % BATCH_SIZE == 0) {
                        writer.flush();
                    }
                }
                
                writer.close();
            }
            
            auto wal_write_time = std::chrono::high_resolution_clock::now();
            auto wal_write_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                wal_write_time - start_time);
            
            // 测试 WAL 读取性能
            start_time = std::chrono::high_resolution_clock::now();
            
            {
                WALReader reader(wal_file);
                assert(reader.open());
                
                int read_count = 0;
                while (!reader.eof()) {
                    auto entry = reader.read_next_entry();
                    if (entry) {
                        read_count++;
                    }
                }
                
                reader.close();
                assert(read_count == NUM_OPERATIONS);
            }
            
            auto wal_read_time = std::chrono::high_resolution_clock::now();
            auto wal_read_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                wal_read_time - start_time);
            
            // 测试快照性能
            std::string snapshot_dir = test_dir + "/snapshots";
            std::filesystem::create_directories(snapshot_dir);
            
            start_time = std::chrono::high_resolution_clock::now();
            
            {
                SnapshotManager manager(snapshot_dir);
                assert(manager.initialize());
                
                std::map<std::string, std::string> large_data;
                for (int i = 0; i < 1000; ++i) {
                    large_data["perf_key_" + std::to_string(i)] = 
                        "perf_value_" + std::to_string(i);
                }
                
                assert(manager.create_snapshot(large_data));
            }
            
            auto snapshot_time = std::chrono::high_resolution_clock::now();
            auto snapshot_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                snapshot_time - start_time);
            
            // 测试恢复性能
            start_time = std::chrono::high_resolution_clock::now();
            
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = test_dir;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> recovered_data;
            
            auto status = recovery.recover_data(recovered_data);
            assert(status == RecoveryStatus::SUCCESS);
            
            auto recovery_time = std::chrono::high_resolution_clock::now();
            auto recovery_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                recovery_time - start_time);
            
            // 输出性能指标
            std::cout << "Performance Results:" << std::endl;
            std::cout << "  WAL Write: " << NUM_OPERATIONS << " ops in " 
                      << wal_write_duration.count() << "ms (" 
                      << (NUM_OPERATIONS * 1000.0 / wal_write_duration.count()) << " ops/s)" << std::endl;
            
            std::cout << "  WAL Read: " << NUM_OPERATIONS << " ops in " 
                      << wal_read_duration.count() << "ms (" 
                      << (NUM_OPERATIONS * 1000.0 / wal_read_duration.count()) << " ops/s)" << std::endl;
            
            std::cout << "  Snapshot: 1000 keys in " << snapshot_duration.count() << "ms" << std::endl;
            std::cout << "  Recovery: " << recovered_data.size() << " keys in " 
                      << recovery_duration.count() << "ms" << std::endl;
            
            auto stats = recovery.get_stats();
            std::cout << "  Recovery Stats: " << stats.snapshot_entries_loaded 
                      << " snapshot entries, " << stats.wal_entries_replayed 
                      << " WAL entries" << std::endl;
            
            std::cout << "✅ Performance test completed" << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Performance test failed: " << e.what() << std::endl;
            throw;
        }
        
        // 清理
        std::filesystem::remove_all(test_dir);
    }
};

int main() {
    std::cout << "=== Persistence Comprehensive Test Suite ===" << std::endl;
    
    try {
        PersistenceTester::runAllTests();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Persistence test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
