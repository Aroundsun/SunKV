/**
 * @file Buffer.h
 * @brief SunKV 网络缓冲区系统
 * 
 * 本文件包含网络缓冲区的实现，提供：
 * - 高效的数据读写操作
 * - 自动缓冲区管理
 * - CRLF 查找功能
 * - 文件描述符读写
 * - 缓冲区动态扩容
 */

#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cassert> 

/**
 * @class Buffer
 * @brief 网络缓冲区类
 * 
 * 提供高效的数据读写操作，支持动态扩容和预留空间
 */
class Buffer {
public:
    static const size_t kInitialSize = 1024;           ///< 初始缓冲区大小
    static const size_t kCheapPrepend = 8;              ///< 预留空间大小
    static const char kCRLF[];                          ///< CRLF 标记
    

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
    

    void swap(Buffer& rhs) {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }
    

    size_t readableBytes() const {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const {
        return buffer_.size() - writerIndex_;
    }
    
    /**
     * @brief 获取可预留字节数（前面可以添加数据的空间）
     * @return 可预留字节数
     */
    size_t prependableBytes() const {
        return readerIndex_;
    }
    
    /**
     * @brief 获取可读数据的指针
     * @return 可读数据指针
     */
    const char* peek() const {
        return begin() + readerIndex_;
    }
    
    /**
     * @brief 查找 CRLF 标记
     * @return CRLF 位置指针，未找到返回 nullptr
     */
    const char* findCRLF() const {
        const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }
    
    /**
     * @brief 从指定位置开始查找 CRLF 标记
     * @param start 查找起始位置
     * @return CRLF 位置指针，未找到返回 nullptr
     */
    const char* findCRLF(const char* start) const {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }
    
    /**
     * @brief 获取可写数据的指针
     * @return 可写数据指针
     */
    char* beginWrite() {
        return begin() + writerIndex_;
    }
    
    /**
     * @brief 获取可写数据的指针（const 版本）
     * @return 可写数据指针
     */
    const char* beginWrite() const {
        return begin() + writerIndex_;
    }
    
    /**
     * @brief 标记已写入 len 字节
     * @param len 写入的字节数
     */
    void hasWritten(size_t len) {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }
    
    /**
     * @brief 撤销写入 len 字节
     * @param len 撤销的字节数
     */
    void unwrite(size_t len) {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }
    
    /**
     * @brief 读取 len 字节
     * @param len 要读取的字节数
     */
    void retrieve(size_t len) {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            readerIndex_ = writerIndex_ = kCheapPrepend;
        }
    }
    
    /**
     * @brief 读取到指定位置
     * @param end 结束位置
     */
    void retrieveUntil(const char* end) {
        retrieve(end - peek());
    }
    
    /**
     * @brief 读取所有数据
     */
    void retrieveAll() {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }
    
    /**
     * @brief 读取 len 字节并转换为字符串
     * @param len 要读取的字节数
     * @return 读取的字符串
     */
    std::string retrieveAsString(size_t len) {
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }
    
    /**
     * @brief 读取所有数据并转换为字符串
     * @return 读取的字符串
     */
    std::string retrieveAllAsString() {
        std::string result(peek(), readableBytes());
        retrieveAll();
        return result;
    }
    
    /**
     * @brief 追加数据到缓冲区
     * @param data 数据指针
     * @param len 数据长度
     */
    void append(const char* data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, begin() + writerIndex_);
        writerIndex_ += len;
    }
    
    /**
     * @brief 追加字符串到缓冲区
     * @param data 要追加的字符串
     */
    void append(const std::string& data) {
        append(data.data(), data.length());
    }
    
    /**
     * @brief 在缓冲区前面添加数据
     * @param data 数据指针
     * @param len 数据长度
     */
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
    
    /**
     * @brief 在缓冲区前面添加字符串
     * @param data 要添加的字符串
     */
    void prepend(const std::string& data) {
        prepend(data.data(), data.length());
    }
    
    /**
     * @brief 从文件描述符读取数据
     * @param fd 文件描述符
     * @param savedErrno 保存的错误码
     * @return 读取的字节数
     */
    ssize_t readFd(int fd, int* savedErrno);
    
    /**
     * @brief 向文件描述符写入数据
     * @param fd 文件描述符
     * @param savedErrno 保存的错误码
     * @return 写入的字节数
     */
    ssize_t writeFd(int fd, int* savedErrno);

private:
    /**
     * @brief 获取缓冲区起始指针（const 版本）
     * @return 缓冲区起始指针
     */
    const char* begin() const { return buffer_.data(); }
    
    /**
     * @brief 获取缓冲区起始指针
     * @return 缓冲区起始指针
     */
    char* begin() { return buffer_.data(); }
    
    /**
     * @brief 确保有足够的可写空间
     * @param len 需要的空间大小
     */
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            size_t readable = readableBytes();
            const size_t required = readable + len + kCheapPrepend;

            // 优先复用前置空间，避免不必要的内存重新分配
            if (prependableBytes() + writableBytes() >= len + kCheapPrepend) {
                std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
                readerIndex_ = kCheapPrepend;
                writerIndex_ = readerIndex_ + readable;
                return;
            }

            size_t newSize = std::max(buffer_.size() * 2, required);  // 减少扩容次数
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
    
    std::vector<char> buffer_;      ///< 缓冲区数据
    size_t readerIndex_;            ///< 读索引
    size_t writerIndex_;            ///< 写索引
};
