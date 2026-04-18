/**
 * @file TcpConnection.h
 * @brief SunKV TCP 连接管理系统
 * 
 * 本文件包含 TCP 连接的实现，提供：
 * - TCP 连接状态管理
 * - 自动缓冲区管理
 * - 异步读写操作
 * - 连接生命周期管理
 * - 回调函数机制
 */

#pragma once

#include <memory>
#include <string>
#include <atomic>
#include "Buffer.h"

/// 前向声明
class EventLoop;
class Channel;
class Socket;

/**
 * @enum TcpConnectionState
 * @brief TCP 连接状态枚举
 * 
 * 定义了 TCP 连接的各种状态
 */
enum class TcpConnectionState {
    kConnecting,    ///< 连接建立中
    kConnected,     ///< 已连接
    kDisconnecting, ///< 断开连接中
    kDisconnected  ///< 已断开
};

/**
 * @class TcpConnection
 * @brief TCP 连接类
 * 
 * 管理 TCP 连接的完整生命周期，包括连接建立、数据传输和连接关闭
 */
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;                        ///< TCP 连接智能指针类型
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;          ///< 连接回调函数类型
    using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*, size_t)>; ///< 消息回调函数类型
    using CloseCallback = std::function<void(const TcpConnectionPtr&)>;                ///< 关闭回调函数类型
    using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;       ///< 写完成回调函数类型
    
    /**
     * @brief 构造函数
     * @param loop 事件循环指针
     * @param name 连接名称
     * @param sockfd 套接字文件描述符
     * @param localAddr 本地地址
     * @param peerAddr 对端地址
     */
    TcpConnection(EventLoop* loop, const std::string& name, int sockfd, 
                  const std::string& localAddr, const std::string& peerAddr);
    
    /**
     * @brief 析构函数
     */
    ~TcpConnection();
    
    // 禁止拷贝和赋值
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    
    /**
     * @brief 获取事件循环指针
     * @return 事件循环指针
     */
    EventLoop* getLoop() const { return loop_; }
    
    /**
     * @brief 获取连接名称
     * @return 连接名称
     */
    const std::string& name() const { return name_; }
    
    /**
     * @brief 获取本地地址
     * @return 本地地址字符串
     */
    const std::string& localAddress() const { return localAddr_; }
    
    /**
     * @brief 获取对端地址
     * @return 对端地址字符串
     */
    const std::string& peerAddress() const { return peerAddr_; }
    
    /**
     * @brief 检查是否已连接
     * @return 是否已连接
     */
    bool connected() const { return state_ == TcpConnectionState::kConnected; }
    
    /**
     * @brief 检查是否已断开
     * @return 是否已断开
     */
    bool disconnected() const { return state_ == TcpConnectionState::kDisconnected; }
    
    /**
     * @brief 设置连接回调函数
     * @param cb 回调函数
     */
    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    
    /**
     * @brief 设置消息回调函数
     * @param cb 回调函数
     */
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    
    /**
     * @brief 设置写完成回调函数
     * @param cb 回调函数
     */
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }
    
    /**
     * @brief 设置关闭回调函数
     * @param cb 回调函数
     */
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
    
    /**
     * @brief 连接建立时调用
     */
    void connectEstablished();
    
    /**
     * @brief 连接销毁时调用
     */
    void connectDestroyed();
    
    /**
     * @brief 发送字符串消息
     * @param message 要发送的消息
     */
    void send(const std::string& message);
    
    /**
     * @brief 发送数据
     * @param data 数据指针
     * @param len 数据长度
     */
    void send(const void* data, size_t len);
    
    /**
     * @brief 发送缓冲区数据
     * @param buffer 缓冲区指针
     */
    void send(Buffer* buffer);
    
    /**
     * @brief 优雅关闭连接
     */
    void shutdown();
    
    /**
     * @brief 强制关闭连接
     */
    void forceClose();
    
    /**
     * @brief 设置 TCP_NODELAY 选项
     * @param on 是否开启
     */
    void setTcpNoDelay(bool on);
    
    /**
     * @brief 设置 SO_KEEPALIVE 选项
     * @param on 是否开启
     */
    void setKeepAlive(bool on);
    
    /**
     * @brief 检查是否正在读取
     * @return 是否正在读取
     */
    bool isReading() const { return reading_; }
    
    /**
     * @brief 开始读取数据
     */
    void startRead();
    
    /**
     * @brief 停止读取数据
     */
    void stopRead();
    
    /**
     * @brief 在事件循环中开始读取
     */
    void startReadInLoop();
    
    /**
     * @brief 在事件循环中停止读取
     */
    void stopReadInLoop();

private:
    /// 事件处理方法
    void handleRead();                                    ///< 处理读事件
    void handleWrite();                                   ///< 处理写事件
    void handleClose();                                   ///< 处理关闭事件
    void handleError();                                   ///< 处理错误事件
    
    /// 发送数据到内核缓冲区
    void sendInLoop(const std::string& message);         ///< 在事件循环中发送字符串
    void sendInLoop(const void* data, size_t len);        ///< 在事件循环中发送数据
    bool enforceOutputBackpressure_();                    ///< 输出缓冲背压保护
    void shutdownInLoop();                                ///< 在事件循环中关闭连接
    void forceCloseInLoop();                              ///< 在事件循环中强制关闭
    
    /**
     * @brief 设置连接状态
     * @param state 新状态
     */
    void setState(TcpConnectionState state) { state_ = state; }
    
    EventLoop* loop_;                                    ///< 所属事件循环
    const std::string name_;                              ///< 连接名称
    std::atomic<TcpConnectionState> state_;               ///< 连接状态
    bool reading_;                                        ///< 是否正在读取
    
    std::unique_ptr<Socket> socket_;                      ///< 套接字对象
    std::unique_ptr<Channel> channel_;                    ///< 事件通道对象
    
    const std::string localAddr_;                         ///< 本地地址
    const std::string peerAddr_;                          ///< 对端地址
    
    Buffer inputBuffer_;                                 ///< 输入缓冲区
    Buffer outputBuffer_;                                ///< 输出缓冲区
    static constexpr size_t kHighWaterMarkBytes_ = 8 * 1024 * 1024; ///< 输出高水位
    
    ConnectionCallback connectionCallback_;               ///< 连接回调
    MessageCallback messageCallback_;                     ///< 消息回调
    CloseCallback closeCallback_;                         ///< 关闭回调
    WriteCompleteCallback writeCompleteCallback_;          ///< 写完成回调
    
    static std::atomic<int64_t> s_numCreated_;            ///< 已创建的连接数统计
};
