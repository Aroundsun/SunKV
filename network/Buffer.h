#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cassert>
#include "logger.h"

// 网络缓冲区类，用于高效的数据读写
class Buffer {
public:
    static const size_t kInitialSize = 1024;           // 初始缓冲区大小
    static const size_t kCheapPrepend = 8;              // 预留空间大小
    static const char kCRLF[];                          // CRLF 标记
    
    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend) {
        assert(readableBytes() == 0);
        assert(writableBytes() == initialSize);
        assert(prependableBytes() == kCheapPrepend);
    }
    
    // 默认拷贝构造和赋值
    Buffer(const Buffer&) = default;
    Buffer& operator=(const Buffer&) = default;
    
    // 交换两个缓冲区
    void swap(Buffer& rhs) {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }
    
    // 可读字节数
    size_t readableBytes() const {
        return writerIndex_ - readerIndex_;
    }
    
    // 可写字节数
    size_t writableBytes() const {
        return buffer_.size() - writerIndex_;
    }
    
    // 可预留字节数（前面可以添加数据的空间）
    size_t prependableBytes() const {
        return readerIndex_;
    }
    
    // 获取可读数据的指针
    const char* peek() const {
        return begin() + readerIndex_;
    }
    
    // 查找指定字符
    const char* findCRLF() const {
        const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }
    
    const char* findCRLF(const char* start) const {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }
    
    // 获取可写数据的指针
    char* beginWrite() {
        return begin() + writerIndex_;
    }
    
    const char* beginWrite() const {
        return begin() + writerIndex_;
    }
    
    // 已经写入 len 字节
    void hasWritten(size_t len) {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }
    
    // 撤销写入 len 字节
    void unwrite(size_t len) {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }
    
    // 读取 len 字节
    void retrieve(size_t len) {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }
    
    // 读取直到指定位置
    void retrieveUntil(const char* end) {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }
    
    // 读取所有数据
    void retrieveAll() {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }
    
    // 读取所有数据并返回字符串
    std::string retrieveAllAsString() {
        return retrieveAsString(readableBytes());
    }
    
    // 读取 len 字节并返回字符串
    std::string retrieveAsString(size_t len) {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }
    
    // 在前面添加数据
    void prepend(const void* data, size_t len) {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        std::copy(d, d + len, begin() + readerIndex_);
    }
    
    // 缩缓冲区空间
    void shrink(size_t reserve) {
        std::vector<char> buffer(kCheapPrepend + readableBytes() + reserve);
        std::copy(peek(), peek() + readableBytes(), buffer.begin() + kCheapPrepend);
        buffer_.swap(buffer);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend + readableBytes();
    }
    
    // 确保足够的空间
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }
    
    // 添加数据
    void append(const std::string& str) {
        append(str.data(), str.size());
    }
    
    void append(const char* data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }
    
    void append(const void* data, size_t len) {
        append(static_cast<const char*>(data), len);
    }
    
    // 从文件描述符读取数据
    ssize_t readFd(int fd, int* savedErrno);
    
    // 向文件描述符写入数据
    ssize_t writeFd(int fd, int* savedErrno);

private:
    char* begin() {
        return &*buffer_.begin();
    }
    
    const char* begin() const {
        return &*buffer_.begin();
    }
    
    // 分配空间
    void makeSpace(size_t len) {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
            // 需要扩展缓冲区
            buffer_.resize(writerIndex_ + len);
        } else {
            // 移动数据到前面
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
            assert(readable == readableBytes());
        }
    }
    
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};
