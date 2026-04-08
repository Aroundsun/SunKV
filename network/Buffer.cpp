#include "Buffer.h"
#include "../common/MemoryPool.h"
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

const char Buffer::kCRLF[] = "\r\n";

// 从文件描述符读取数据
ssize_t Buffer::readFd(int fd, int* savedErrno) {
    // 通过线程本地内存池复用临时缓冲，减少高频路径分配开销
    auto extraBufLease = ThreadLocalBufferPool::instance().acquire(65536);
    const size_t writable = writableBytes();

    // 当当前缓冲区可写空间已足够大时，直接 read，减少 iovec 组装成本
    if (writable >= extraBufLease.size()) {
        const ssize_t n = ::read(fd, beginWrite(), writable);
        if (n < 0) {
            *savedErrno = errno;
        } else {
            writerIndex_ += static_cast<size_t>(n);
        }
        return n;
    }

    struct iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extraBufLease.data();
    vec[1].iov_len = extraBufLease.size();

    // 使用 readv 一次性读取到主缓冲区和额外缓冲区
    const ssize_t n = ::readv(fd, vec, 2);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 数据完全写入到缓冲区
        writerIndex_ += static_cast<size_t>(n);
    } else {
        // 数据部分写入到缓冲区，部分写入到额外缓冲区
        writerIndex_ = buffer_.size();
        append(extraBufLease.data(), static_cast<size_t>(n) - writable);
    }
    
    return n;
}

// 向文件描述符写入数据
ssize_t Buffer::writeFd(int fd, int* savedErrno) {
    const ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) {
        *savedErrno = errno;
    } else {
        retrieve(static_cast<size_t>(n));
    }
    return n;
}
