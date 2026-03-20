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
        }
        readerIndex_ += len;
        if (readerIndex_ == writerIndex_) {
            readerIndex_ = writerIndex_ = kCheapPrepend;
        }
    }
    
    void retrieveUntil(const char* end) {
        retrieve(end - peek());
    }
    
    void retrieveAll() {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }
    
    std::string retrieveAsString(size_t len) {
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }
    
    std::string retrieveAllAsString() {
        std::string result(peek(), readableBytes());
        retrieveAll();
        return result;
    }
    
    void append(const char* data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, begin() + writerIndex_);
        writerIndex_ += len;
    }
    
    void append(const std::string& data) {
        append(data.data(), data.length());
    }
    
    void prepend(const char* data, size_t len) {
        if (len > prependableBytes()) {
            // 空间不足，需要移动数据
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
        readerIndex_ -= len;
        std::copy(data, data + len, begin() + readerIndex_);
    }
    
    void prepend(const std::string& data) {
        prepend(data.data(), data.length());
    }
    
    // 写入文件描述符
    ssize_t readFd(int fd, int* savedErrno);
    
    // 写入文件描述符
    ssize_t writeFd(int fd, int* savedErrno);

private:
    const char* begin() const { return buffer_.data(); }
    char* begin() { return buffer_.data(); }
    
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            size_t readable = readableBytes();
            size_t newSize = buffer_.size();
            
            // 如果可读数据很大，需要重新分配
            if (newSize < readable + len + kCheapPrepend) {
                newSize = readable + len + kCheapPrepend;
            }
            newSize = std::max(newSize, buffer_.size() * 2);  // 双倍增长
            
            std::vector<char> newBuffer(newSize);
            
            // 复制可读数据到新缓冲区
            if (readable > 0) {
                std::copy(begin() + readerIndex_, begin() + writerIndex_, newBuffer.begin() + kCheapPrepend);
            }
            
            buffer_ = std::move(newBuffer);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }
    
    static const char kCRLF[];
    
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};
