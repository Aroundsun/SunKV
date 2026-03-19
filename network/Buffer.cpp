#include "Buffer.h"
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

const char Buffer::kCRLF[] = "\r\n";

// 从文件描述符读取数据
ssize_t Buffer::readFd(int fd, int* savedErrno) {
    char extrabuf[65536];  // 64K 栈上缓冲区
    struct iovec vec[2];
    
    const size_t writable = writableBytes();
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    
    // 使用 readv 一次性读取到两个缓冲区
    const ssize_t n = ::readv(fd, vec, 2);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 数据完全写入到缓冲区
        writerIndex_ += n;
    } else {
        // 数据部分写入到缓冲区，部分写入到额外缓冲区
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    
    return n;
}

// 向文件描述符写入数据
ssize_t Buffer::writeFd(int fd, int* savedErrno) {
    const size_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) {
        *savedErrno = errno;
    } else {
        readerIndex_ += n;
    }
    return n;
}
