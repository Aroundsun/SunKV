#pragma once

#include <memory>
#include <string>
#include <atomic>
#include "Buffer.h"
#include "logger.h"

class EventLoop;
class Channel;
class Socket;

// TCP 连接状态
enum class TcpConnectionState {
    kConnecting,    // 连接中
    kConnected,     // 已连接
    kDisconnecting, // 断开连接中
    kDisconnected  // 已断开
};

// TCP 连接类
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*, size_t)>;
    using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
    using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
    
    TcpConnection(EventLoop* loop, const std::string& name, int sockfd, 
                  const std::string& localAddr, const std::string& peerAddr);
    ~TcpConnection();
    
    // 禁止拷贝和赋值
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    
    // 获取连接信息
    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const std::string& localAddress() const { return localAddr_; }
    const std::string& peerAddress() const { return peerAddr_; }
    bool connected() const { return state_ == TcpConnectionState::kConnected; }
    bool disconnected() const { return state_ == TcpConnectionState::kDisconnected; }
    
    // 设置回调函数
    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }
    
    // 连接建立
    void connectEstablished();
    
    // 连接销毁
    void connectDestroyed();
    
    // 发送数据
    void send(const std::string& message);
    void send(const void* data, size_t len);
    void send(Buffer* buffer);
    
    // 关闭连接
    void shutdown();
    void forceClose();
    void setTcpNoDelay(bool on);
    void setKeepAlive(bool on);
    
    // 读取状态
    bool isReading() const { return reading_; }
    void startRead();
    void stopRead();
    
    // 内部方法
    void startReadInLoop();
    void stopReadInLoop();

private:
    // 事件处理
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    
    // 发送数据到内核缓冲区
    void sendInLoop(const std::string& message);
    void sendInLoop(const void* data, size_t len);
    void shutdownInLoop();
    void forceCloseInLoop();
    
    // 状态设置
    void setState(TcpConnectionState state) { state_ = state; }
    
    EventLoop* loop_;
    const std::string name_;
    std::atomic<TcpConnectionState> state_;
    bool reading_;
    
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;
    
    const std::string localAddr_;
    const std::string peerAddr_;
    
    Buffer inputBuffer_;
    Buffer outputBuffer_;
    
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    
    static std::atomic<int64_t> s_numCreated_;
};
