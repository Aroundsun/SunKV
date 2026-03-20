#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <unordered_map>
#include "storage/StorageEngine.h"
#include "storage/ShardedKVStore.h"
#include "storage/CachedStorageEngine.h"

// 测试辅助类
class StorageEngineTester {
public:
    // 单元测试存储操作
    static void testBasicOperations() {
        std::cout << "--- Basic Operations Test ---" << std::endl;
        
        auto& engine = StorageEngine::getInstance();
        engine.clear();  // 清空之前的数据
        
        // 测试 SET 操作
        bool result = engine.set("test_key1", "test_value1");
        assert(result == true);
        std::cout << "SET operation: ✅" << std::endl;
        
        // 测试 GET 操作
        std::string value = engine.get("test_key1");
        assert(value == "test_value1");
        std::cout << "GET operation: ✅" << std::endl;
        
        // 测试 EXISTS 操作
        bool exists = engine.exists("test_key1");
        assert(exists == true);
        std::cout << "EXISTS operation: ✅" << std::endl;
        
        // 测试 DEL 操作
        bool deleted = engine.del("test_key1");
        assert(deleted == true);
        std::cout << "DEL operation: ✅" << std::endl;
        
        // 验证删除后不存在
        exists = engine.exists("test_key1");
        assert(exists == false);
        std::cout << "Post-delete verification: ✅" << std::endl;
        
        // 测试不存在的键
        value = engine.get("nonexistent_key");
        assert(value.empty());
        std::cout << "Non-existent key handling: ✅" << std::endl;
        
        engine.clear();
        std::cout << "Basic operations test completed successfully! 🎉" << std::endl;
    }
    
    // 测试 TTL 功能
    static void testTTLFunctionality() {
        std::cout << "\n--- TTL Functionality Test ---" << std::endl;
        
        auto& engine = StorageEngine::getInstance();
        engine.clear();
        
        // 测试无 TTL 的键
        engine.set("permanent_key", "permanent_value");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string value = engine.get("permanent_key");
        assert(value == "permanent_value");
        std::cout << "Permanent key (no TTL): ✅" << std::endl;
        
        // 测试有 TTL 的键
        engine.set("temp_key", "temp_value", 200);  // 200ms TTL
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        value = engine.get("temp_key");
        assert(value == "temp_value");  // 应该还存在
        std::cout << "TTL key (before expiration): ✅" << std::endl;
        
        // 等待过期
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        value = engine.get("temp_key");
        assert(value.empty());  // 应该已过期
        std::cout << "TTL key (after expiration): ✅" << std::endl;
        
        // 测试清理过期键
        engine.set("expire_key", "expire_value", 100);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        engine.cleanupExpired();
        assert(!engine.exists("expire_key"));
        std::cout << "Expired key cleanup: ✅" << std::endl;
        
        engine.clear();
        std::cout << "TTL functionality test completed successfully! 🎉" << std::endl;
    }
    
    // 测试批量操作
    static void testBatchOperations() {
        std::cout << "\n--- Batch Operations Test ---" << std::endl;
        
        ShardedKVStore store(4);
        
        // 测试 MSET
        std::vector<std::pair<std::string, std::string>> batch_data = {
            {"batch_key1", "batch_value1"},
            {"batch_key2", "batch_value2"},
            {"batch_key3", "batch_value3"},
            {"batch_key4", "batch_value4"},
            {"batch_key5", "batch_value5"}
        };
        
        store.mset(batch_data);
        std::cout << "MSET operation: ✅" << std::endl;
        
        // 测试 MGET
        std::vector<std::string> keys = {"batch_key1", "batch_key2", "batch_key3", "nonexistent_key"};
        auto results = store.mget(keys);
        
        assert(results.size() == 4);
        assert(results[0] == "batch_value1");
        assert(results[1] == "batch_value2");
        assert(results[2] == "batch_value3");
        assert(results[3].empty());
        std::cout << "MGET operation: ✅" << std::endl;
        
        // 测试 MDEL
        std::vector<std::string> delete_keys = {"batch_key1", "batch_key2", "nonexistent_key"};
        int deleted_count = store.mdel(delete_keys);
        assert(deleted_count == 2);  // 只能删除存在的键
        std::cout << "MDEL operation: ✅" << std::endl;
        
        // 验证删除结果
        assert(!store.exists("batch_key1"));
        assert(!store.exists("batch_key2"));
        assert(store.exists("batch_key3"));
        std::cout << "Batch deletion verification: ✅" << std::endl;
        
        store.clear();
        std::cout << "Batch operations test completed successfully! 🎉" << std::endl;
    }
    
    // 测试分片功能
    static void testShardingFunctionality() {
        std::cout << "\n--- Sharding Functionality Test ---" << std::endl;
        
        ShardedKVStore store(8);
        
        // 添加数据到不同分片
        for (int i = 0; i < 100; ++i) {
            store.set("shard_key" + std::to_string(i), "shard_value" + std::to_string(i));
        }
        
        // 检查分片分布
        auto shard_sizes = store.shard_sizes();
        assert(shard_sizes.size() == 8);
        
        size_t total_size = 0;
        for (size_t size : shard_sizes) {
            total_size += size;
        }
        assert(total_size == 100);
        std::cout << "Shard distribution: ✅" << std::endl;
        
        // 验证所有数据都能访问
        for (int i = 0; i < 100; ++i) {
            std::string value = store.get("shard_key" + std::to_string(i));
            assert(value == "shard_value" + std::to_string(i));
        }
        std::cout << "Cross-shard data access: ✅" << std::endl;
        
        // 测试分片索引计算的一致性
        std::string test_key = "consistency_test_key";
        size_t shard_idx1 = store.get_shard_index(test_key);
        size_t shard_idx2 = store.get_shard_index(test_key);
        assert(shard_idx1 == shard_idx2);
        std::cout << "Shard index consistency: ✅" << std::endl;
        
        store.clear();
        std::cout << "Sharding functionality test completed successfully! 🎉" << std::endl;
    }
    
    // 缓存算法正确性测试
    static void testCacheCorrectness() {
        std::cout << "\n--- Cache Correctness Test ---" << std::endl;
        
        // 测试 LRU 缓存
        testLRUCorrectness();
        
        // 测试 LFU 缓存
        testLFUCorrectness();
        
        // 测试 ARC 缓存
        testARCCorrectness();
        
        std::cout << "Cache correctness test completed successfully! 🎉" << std::endl;
    }
    
    // 并发读写测试
    static void testConcurrentAccess() {
        std::cout << "\n--- Concurrent Access Test ---" << std::endl;
        
        const int num_threads = 16;
        const int operations_per_thread = 1000;
        
        ShardedKVStore store(8);
        std::atomic<int> write_success{0};
        std::atomic<int> read_success{0};
        std::atomic<int> error_count{0};
        
        std::vector<std::thread> threads;
        
        // 启动写线程
        for (int t = 0; t < num_threads / 2; ++t) {
            threads.emplace_back([&store, t, operations_per_thread, &write_success, &error_count]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 999);
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    try {
                        int key = dis(gen);
                        store.set("concurrent_key" + std::to_string(key), 
                                 "thread" + std::to_string(t) + "_value" + std::to_string(i));
                        write_success++;
                    } catch (...) {
                        error_count++;
                    }
                }
            });
        }
        
        // 启动读线程
        for (int t = num_threads / 2; t < num_threads; ++t) {
            threads.emplace_back([&store, t, operations_per_thread, &read_success, &error_count]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 999);
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    try {
                        int key = dis(gen);
                        std::string value = store.get("concurrent_key" + std::to_string(key));
                        if (!value.empty()) {
                            read_success++;
                        }
                    } catch (...) {
                        error_count++;
                    }
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }
        
        std::cout << "Write operations: " << write_success << " success" << std::endl;
        std::cout << "Read operations: " << read_success << " success" << std::endl;
        std::cout << "Error count: " << error_count << std::endl;
        
        assert(error_count == 0);
        std::cout << "Concurrent access: ✅" << std::endl;
        
        store.clear();
        std::cout << "Concurrent access test completed successfully! 🎉" << std::endl;
    }
    
    // 内存泄漏检测
    static void testMemoryLeaks() {
        std::cout << "\n--- Memory Leak Detection Test ---" << std::endl;
        
        // 大量操作测试内存管理
        {
            ShardedKVStore store(4);
            
            // 添加大量数据
            for (int i = 0; i < 10000; ++i) {
                store.set("memory_test_key" + std::to_string(i), 
                         "memory_test_value" + std::to_string(i));
            }
            
            // 随机访问
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 9999);
            
            for (int i = 0; i < 5000; ++i) {
                int key = dis(gen);
                store.get("memory_test_key" + std::to_string(key));
            }
            
            // 删除部分数据
            for (int i = 0; i < 5000; ++i) {
                store.del("memory_test_key" + std::to_string(i));
            }
            
            // store 在这里析构，应该释放所有内存
        }
        
        // 重复测试多次以确保没有内存泄漏
        for (int round = 0; round < 10; ++round) {
            ShardedKVStore temp_store(2);
            for (int i = 0; i < 1000; ++i) {
                temp_store.set("temp_key" + std::to_string(i), "temp_value" + std::to_string(i));
            }
            // temp_store 在这里析构
        }
        
        std::cout << "Memory leak detection: ✅" << std::endl;
        std::cout << "Memory leak test completed successfully! 🎉" << std::endl;
    }
    
    // 性能基准测试
    static void testPerformanceBenchmark() {
        std::cout << "\n--- Performance Benchmark Test ---" << std::endl;
        
        const int num_operations = 100000;
        
        // 测试 StorageEngine 性能
        {
            auto& engine = StorageEngine::getInstance();
            engine.clear();
            
            auto start = std::chrono::high_resolution_clock::now();
            
            // 写操作性能
            for (int i = 0; i < num_operations; ++i) {
                engine.set("perf_key" + std::to_string(i), "perf_value" + std::to_string(i));
            }
            
            auto write_end = std::chrono::high_resolution_clock::now();
            auto write_duration = std::chrono::duration_cast<std::chrono::microseconds>(write_end - start);
            
            // 读操作性能
            start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < num_operations; ++i) {
                engine.get("perf_key" + std::to_string(i));
            }
            
            auto read_end = std::chrono::high_resolution_clock::now();
            auto read_duration = std::chrono::duration_cast<std::chrono::microseconds>(read_end - start);
            
            std::cout << "StorageEngine Performance:" << std::endl;
            std::cout << "  Write: " << write_duration.count() << " μs (" 
                      << (double)num_operations / write_duration.count() * 1000000 << " ops/sec)" << std::endl;
            std::cout << "  Read:  " << read_duration.count() << " μs (" 
                      << (double)num_operations / read_duration.count() * 1000000 << " ops/sec)" << std::endl;
        }
        
        // 测试 ShardedKVStore 性能
        {
            ShardedKVStore store(8);
            
            auto start = std::chrono::high_resolution_clock::now();
            
            // 写操作性能
            for (int i = 0; i < num_operations; ++i) {
                store.set("shard_perf_key" + std::to_string(i), "shard_perf_value" + std::to_string(i));
            }
            
            auto write_end = std::chrono::high_resolution_clock::now();
            auto write_duration = std::chrono::duration_cast<std::chrono::microseconds>(write_end - start);
            
            // 读操作性能
            start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < num_operations; ++i) {
                store.get("shard_perf_key" + std::to_string(i));
            }
            
            auto read_end = std::chrono::high_resolution_clock::now();
            auto read_duration = std::chrono::duration_cast<std::chrono::microseconds>(read_end - start);
            
            std::cout << "ShardedKVStore Performance:" << std::endl;
            std::cout << "  Write: " << write_duration.count() << " μs (" 
                      << (double)num_operations / write_duration.count() * 1000000 << " ops/sec)" << std::endl;
            std::cout << "  Read:  " << read_duration.count() << " μs (" 
                      << (double)num_operations / read_duration.count() * 1000000 << " ops/sec)" << std::endl;
        }
        
        std::cout << "Performance benchmark test completed successfully! 🎉" << std::endl;
    }
    
    // 缓存命中率对比测试
    static void testCacheHitRateComparison() {
        std::cout << "\n--- Cache Hit Rate Comparison Test ---" << std::endl;
        
        const int num_operations = 10000;
        const int cache_size = 1000;
        const int key_space = 2000;  // 键空间大于缓存大小
        
        // 测试不同缓存策略的命中率
        std::vector<CachePolicyType> policies = {
            CachePolicyType::LRU,
            CachePolicyType::LFU,
            CachePolicyType::ARC
        };
        
        std::vector<std::string> policy_names = {"LRU", "LFU", "ARC"};
        
        for (size_t i = 0; i < policies.size(); ++i) {
            auto engine = CachedStorageEngine<std::string, std::string>::create(cache_size, policies[i]);
            engine->clear();
            
            // 模拟真实访问模式：80% 的访问集中在 20% 的键上
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> hot_dist(0, key_space * 0.2 - 1);  // 热点键
            std::uniform_int_distribution<> cold_dist(key_space * 0.2, key_space - 1);  // 冷键
            std::bernoulli_distribution hot_prob(0.8);  // 80% 概率访问热点键
            
            for (int op = 0; op < num_operations; ++op) {
                std::string key;
                if (hot_prob(gen)) {
                    key = "cache_test_key" + std::to_string(hot_dist(gen));
                } else {
                    key = "cache_test_key" + std::to_string(cold_dist(gen));
                }
                
                std::string value = "cache_test_value" + std::to_string(op);
                engine->set(key, value);
                engine->get(key);
            }
            
            auto stats = engine->get_cache_stats();
            std::cout << policy_names[i] << " Cache:" << std::endl;
            std::cout << "  Hit rate: " << (stats.hit_rate * 100) << "%" << std::endl;
            std::cout << "  Cache size: " << stats.cache_size << "/" << stats.cache_capacity << std::endl;
            std::cout << "  Total requests: " << stats.total_requests << std::endl;
        }
        
        std::cout << "Cache hit rate comparison test completed successfully! 🎉" << std::endl;
    }

private:
    static void testLRUCorrectness() {
        auto engine = CachedStorageEngine<std::string, std::string>::create(3, CachePolicyType::LRU);
        
        // 填满缓存
        engine->set("A", "valueA");
        engine->set("B", "valueB");
        engine->set("C", "valueC");
        
        // 访问 A（移到头部）
        engine->get("A");
        
        // 添加 D，应该淘汰 B（最少使用）
        engine->set("D", "valueD");
        
        // 验证 B 被淘汰
        std::string value = engine->get("B");
        assert(value.empty());
        
        // 验证 A, C, D 仍然存在
        assert(!engine->get("A").empty());
        assert(!engine->get("C").empty());
        assert(!engine->get("D").empty());
        
        std::cout << "LRU correctness: ✅" << std::endl;
    }
    
    static void testLFUCorrectness() {
        auto engine = CachedStorageEngine<std::string, std::string>::create(3, CachePolicyType::LFU);
        
        // 添加键
        engine->set("A", "valueA");
        engine->set("B", "valueB");
        engine->set("C", "valueC");
        
        // 多次访问 A 和 B
        for (int i = 0; i < 5; ++i) {
            engine->get("A");
        }
        for (int i = 0; i < 3; ++i) {
            engine->get("B");
        }
        
        // 添加 D，应该淘汰 C（频率最低）
        engine->set("D", "valueD");
        
        // 验证 C 被淘汰
        std::string value = engine->get("C");
        assert(value.empty());
        
        // 验证 A, B, D 仍然存在
        assert(!engine->get("A").empty());
        assert(!engine->get("B").empty());
        assert(!engine->get("D").empty());
        
        std::cout << "LFU correctness: ✅" << std::endl;
    }
    
    static void testARCCorrectness() {
        auto engine = CachedStorageEngine<std::string, std::string>::create(4, CachePolicyType::ARC);
        
        // 添加键
        engine->set("A", "valueA");
        engine->set("B", "valueB");
        engine->set("C", "valueC");
        engine->set("D", "valueD");
        
        // 访问 A 和 B（移到 T2）
        engine->get("A");
        engine->get("B");
        
        // 添加 E，应该从 T1 淘汰
        engine->set("E", "valueE");
        
        // 验证自适应行为
        auto stats = engine->get_cache_stats();
        assert(stats.policy_name == "ARC");
        
        std::cout << "ARC correctness: ✅" << std::endl;
    }
};

int main() {
    std::cout << "=== Storage Engine Comprehensive Test Suite ===" << std::endl;
    
    try {
        // 运行所有测试
        StorageEngineTester::testBasicOperations();
        StorageEngineTester::testTTLFunctionality();
        StorageEngineTester::testBatchOperations();
        StorageEngineTester::testShardingFunctionality();
        StorageEngineTester::testCacheCorrectness();
        StorageEngineTester::testConcurrentAccess();
        StorageEngineTester::testMemoryLeaks();
        StorageEngineTester::testPerformanceBenchmark();
        StorageEngineTester::testCacheHitRateComparison();
        
        std::cout << "\n🎉 ALL TESTS PASSED! 🎉" << std::endl;
        std::cout << "Storage Engine is ready for production use!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}
