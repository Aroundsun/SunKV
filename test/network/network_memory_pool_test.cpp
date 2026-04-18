#include <cassert>
#include <iostream>
#include "common/MemoryPool.h"

static void resetPoolForTest() {
    auto& pool = ThreadLocalBufferPool::instance();
    pool.setMaxCachedBlocksPerSize(0);  // 先清空历史缓存
    pool.setMaxCachedBlocksPerSize(8);
    pool.resetStats();
}

static void testAcquireReleaseBasic() {
    resetPoolForTest();
    auto lease = ThreadLocalBufferPool::instance().acquire(1024);
    assert(lease);
    assert(lease.data() != nullptr);
    assert(lease.size() == 1024);
    lease.data()[0] = 'x';
}

static void testReuseSameSizeBlock() {
    resetPoolForTest();
    char* first_ptr = nullptr;
    {
        auto lease = ThreadLocalBufferPool::instance().acquire(4096);
        first_ptr = lease.data();
        assert(first_ptr != nullptr);
    }  // 自动归还

    auto lease2 = ThreadLocalBufferPool::instance().acquire(4096);
    // 同线程同尺寸，期望可复用
    if (lease2.data() != first_ptr) {
        std::cerr << "expected same-size block to be reused" << std::endl;
        std::abort();
    }
}

static void testDifferentSizeIsolation() {
    resetPoolForTest();
    auto a = ThreadLocalBufferPool::instance().acquire(1024);
    auto b = ThreadLocalBufferPool::instance().acquire(2048);
    assert(a.data() != b.data());
}

static void testStatsHitMiss() {
    resetPoolForTest();
    auto& pool = ThreadLocalBufferPool::instance();

    {
        auto a = pool.acquire(512);
        (void)a;
    }
    auto s1 = pool.getStats();
    if (s1.miss_count < 1 || s1.release_count < 1) {
        std::cerr << "expected miss/release stats to increase" << std::endl;
        std::abort();
    }

    {
        auto b = pool.acquire(512);
        (void)b;
    }
    auto s2 = pool.getStats();
    if (s2.hit_count < 1) {
        std::cerr << "expected hit stats to increase" << std::endl;
        std::abort();
    }
}

static void testCapacityLimitAndDiscard() {
    resetPoolForTest();
    auto& pool = ThreadLocalBufferPool::instance();
    pool.setMaxCachedBlocksPerSize(1);

    {
        auto a = pool.acquire(256);
        auto b = pool.acquire(256);
        (void)a;
        (void)b;
    }

    auto stats = pool.getStats();
    if (stats.cached_block_count > 1 || stats.discard_count < 1) {
        std::cerr << "capacity limit/discard stats check failed" << std::endl;
        std::abort();
    }
}

int main() {
    testAcquireReleaseBasic();
    testReuseSameSizeBlock();
    testDifferentSizeIsolation();
    testStatsHitMiss();
    testCapacityLimitAndDiscard();
    std::cout << "Memory pool tests passed." << std::endl;
    return 0;
}
