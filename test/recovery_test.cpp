#include "../storage/Recovery.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <map>

int main() {
    std::cout << "=== Data Recovery Test ===" << std::endl;
    
    try {
        // 准备测试环境
        std::string snapshot_dir = "test_snapshots";
        std::string wal_dir = "test_wal";
        
        std::filesystem::remove_all(snapshot_dir);
        std::filesystem::remove_all(wal_dir);
        std::filesystem::create_directories(snapshot_dir);
        std::filesystem::create_directories(wal_dir);
        
        // 测试1: 空环境恢复
        std::cout << "\n--- Test 1: Empty Environment Recovery ---" << std::endl;
        {
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> data;
            
            auto status = recovery.recover_data(data);
            assert(status == RecoveryStatus::SUCCESS);
            assert(data.empty());
            
            auto stats = recovery.get_stats();
            assert(stats.snapshot_entries_loaded == 0);
            assert(stats.wal_entries_replayed == 0);
            
            std::cout << "✅ Empty environment recovery works" << std::endl;
        }
        
        // 测试2: 从快照恢复
        std::cout << "\n--- Test 2: Snapshot Recovery ---" << std::endl;
        {
            // 创建测试快照
            std::string snapshot_file = snapshot_dir + "/snapshot_20230321_120000_0.snap";
            {
                SnapshotWriter writer(snapshot_file);
                assert(writer.open());
                assert(writer.write_data("key1", "value1", -1));
                assert(writer.write_data("key2", "value2", 3600000));
                assert(writer.write_deleted("deleted_key"));
                writer.close();
            }
            
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> data;
            
            auto status = recovery.recover_data(data);
            assert(status == RecoveryStatus::SUCCESS);
            assert(data.size() == 2);
            assert(data["key1"] == "value1");
            assert(data["key2"] == "value2");
            assert(data.find("deleted_key") == data.end());
            
            auto stats = recovery.get_stats();
            assert(stats.snapshot_entries_loaded == 3);
            assert(stats.wal_entries_replayed == 0);
            
            std::cout << "✅ Snapshot recovery works: " << data.size() << " keys loaded" << std::endl;
        }
        
        // 测试3: WAL 重放
        std::cout << "\n--- Test 3: WAL Replay ---" << std::endl;
        {
            // 创建测试 WAL
            std::string wal_file = wal_dir + "/test_wal.log";
            {
                WALWriter writer(wal_file);
                assert(writer.open());
                assert(writer.write_put("key3", "value3"));
                assert(writer.write_put("key4", "value4"));
                assert(writer.write_delete("key2"));
                writer.close();
            }
            
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> data;
            
            auto status = recovery.recover_data(data);
            assert(status == RecoveryStatus::SUCCESS);
            assert(data.size() == 3);
            assert(data["key1"] == "value1");
            assert(data["key3"] == "value3");
            assert(data["key4"] == "value4");
            assert(data.find("key2") == data.end());  // 被 WAL 删除
            assert(data.find("deleted_key") == data.end());  // 在快照中被删除
            
            auto stats = recovery.get_stats();
            assert(stats.snapshot_entries_loaded == 3);
            assert(stats.wal_entries_replayed == 3);
            
            std::cout << "✅ WAL replay works: " << data.size() << " keys after replay" << std::endl;
        }
        
        // 测试4: 数据一致性检查
        std::cout << "\n--- Test 4: Data Consistency Check ---" << std::endl;
        {
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            options.enable_consistency_check = true;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> data;
            
            auto status = recovery.recover_data(data);
            assert(status == RecoveryStatus::SUCCESS);
            
            // 验证数据一致性
            bool is_consistent = recovery.verify_consistency(data);
            assert(is_consistent);
            
            std::cout << "✅ Data consistency check works" << std::endl;
        }
        
        // 测试5: 错误处理
        std::cout << "\n--- Test 5: Error Handling ---" << std::endl;
        {
            // 创建损坏的快照文件
            std::string corrupted_snapshot = snapshot_dir + "/corrupted.snap";
            {
                std::ofstream file(corrupted_snapshot);
                file << "corrupted data";
                file.close();
            }
            
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            options.skip_corrupted_entries = true;
            
            DataRecovery recovery(options);
            std::map<std::string, std::string> data;
            
            auto status = recovery.recover_data(data);
            assert(status == RecoveryStatus::SUCCESS);
            
            // 应该跳过损坏的快照，使用正常的快照
            assert(data.size() == 3);
            
            std::cout << "✅ Error handling works: skipped corrupted files" << std::endl;
        }
        
        // 测试6: 清理损坏文件
        std::cout << "\n--- Test 6: Cleanup Corrupted Files ---" << std::endl;
        {
            RecoveryOptions options;
            options.snapshot_dir = snapshot_dir;
            options.wal_dir = wal_dir;
            
            DataRecovery recovery(options);
            
            // 创建损坏文件
            std::string corrupted_file = snapshot_dir + "/test_corrupted.snap";
            {
                std::ofstream file(corrupted_file);
                file << "bad data";
                file.close();
            }
            
            // 清理损坏文件
            bool cleaned = recovery.cleanup_corrupted_files();
            
            // 验证损坏文件被删除
            assert(!std::filesystem::exists(corrupted_file));
            
            std::cout << "✅ Cleanup corrupted files works" << std::endl;
        }
        
        // 清理测试环境
        std::filesystem::remove_all(snapshot_dir);
        std::filesystem::remove_all(wal_dir);
        
        std::cout << "\n🎉 ALL RECOVERY TESTS PASSED! 🎉" << std::endl;
        std::cout << "Data recovery functionality is working correctly!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
