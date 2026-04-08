#include "MemoryPool.h"

ThreadLocalBufferPool::Lease::~Lease() {
    if (ptr_ != nullptr) {
        ThreadLocalBufferPool::instance().release(ptr_, size_);
        ptr_ = nullptr;
        size_ = 0;
    }
}

ThreadLocalBufferPool::Lease::Lease(Lease&& other) noexcept
    : ptr_(other.ptr_), size_(other.size_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
}

ThreadLocalBufferPool::Lease& ThreadLocalBufferPool::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        if (ptr_ != nullptr) {
            ThreadLocalBufferPool::instance().release(ptr_, size_);
        }
        ptr_ = other.ptr_;
        size_ = other.size_;
        other.ptr_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

ThreadLocalBufferPool& ThreadLocalBufferPool::instance() {
    static thread_local ThreadLocalBufferPool pool;
    return pool;
}

ThreadLocalBufferPool::Lease ThreadLocalBufferPool::acquire(size_t size) {
    auto& bucket = free_blocks_[size];
    if (!bucket.empty()) {
        std::unique_ptr<char[]> block = std::move(bucket.back());
        bucket.pop_back();
        ++hit_count_;
        return Lease(block.release(), size);
    }
    ++miss_count_;
    return Lease(new char[size], size);
}

void ThreadLocalBufferPool::release(char* ptr, size_t size) {
    if (ptr == nullptr || size == 0) {
        return;
    }
    ++release_count_;
    auto& bucket = free_blocks_[size];
    if (bucket.size() < max_cached_blocks_per_size_) {
        bucket.emplace_back(ptr);
    } else {
        ++discard_count_;
        delete[] ptr;
    }
}

void ThreadLocalBufferPool::setMaxCachedBlocksPerSize(size_t limit) {
    max_cached_blocks_per_size_ = limit;
    if (limit == 0) {
        for (auto& entry : free_blocks_) {
            entry.second.clear();
        }
        return;
    }
    for (auto& entry : free_blocks_) {
        auto& bucket = entry.second;
        while (bucket.size() > limit) {
            bucket.pop_back();
        }
    }
}

ThreadLocalBufferPool::Stats ThreadLocalBufferPool::getStats() const {
    Stats stats;
    stats.hit_count = hit_count_;
    stats.miss_count = miss_count_;
    stats.release_count = release_count_;
    stats.discard_count = discard_count_;

    size_t cached = 0;
    for (const auto& entry : free_blocks_) {
        cached += entry.second.size();
    }
    stats.cached_block_count = cached;
    return stats;
}

void ThreadLocalBufferPool::resetStats() {
    hit_count_ = 0;
    miss_count_ = 0;
    release_count_ = 0;
    discard_count_ = 0;
}
