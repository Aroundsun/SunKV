#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief 线程本地固定块内存池
 *
 * 用于复用高频临时缓冲区，降低重复分配释放带来的开销。
 */
class ThreadLocalBufferPool {
public:
    struct Stats {
        size_t hit_count{0};          ///< 命中缓存次数
        size_t miss_count{0};         ///< 未命中缓存次数
        size_t release_count{0};      ///< 归还次数
        size_t discard_count{0};      ///< 因容量上限被丢弃次数
        size_t cached_block_count{0}; ///< 当前缓存块数量
    };

    /**
     * @brief 内存块租约（RAII）
     *
     * 构造时获取，析构时自动归还到线程本地内存池。
     */
    class Lease {
    public:
        Lease() = default;
        Lease(char* ptr, size_t size) : ptr_(ptr), size_(size) {}
        ~Lease();

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;

        char* data() { return ptr_; }
        const char* data() const { return ptr_; }
        size_t size() const { return size_; }
        explicit operator bool() const { return ptr_ != nullptr; }

    private:
        char* ptr_{nullptr};
        size_t size_{0};
    };

    static ThreadLocalBufferPool& instance();
    Lease acquire(size_t size);
    void release(char* ptr, size_t size);
    void setMaxCachedBlocksPerSize(size_t limit);
    size_t maxCachedBlocksPerSize() const { return max_cached_blocks_per_size_; }
    Stats getStats() const;
    void resetStats();

private:
    size_t max_cached_blocks_per_size_{8};
    std::unordered_map<size_t, std::vector<std::unique_ptr<char[]>>> free_blocks_;
    size_t hit_count_{0};
    size_t miss_count_{0};
    size_t release_count_{0};
    size_t discard_count_{0};
};
