#pragma once

#include <string>
#include <memory>
#include <map>
#include <atomic>
#include "logger.h"

class EventLoop;
class Acceptor;
class TcpConnection;
class EventLoopThreadPool;

// TCP 服务器类
class TcpServer {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;
    
    TcpServer(EventLoop* loop, const std::string& name, const std::string& listenAddr, uint16_t listenPort);
    ~TcpServer();
    
    // 禁止拷贝和赋值
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    
    // 设置回调函数
    void setConnectionCallback(const std::function<void(const std::shared_ptr<TcpConnection>&)>& cb);
    void setMessageCallback(const std::function<void(const std::shared_ptr<TcpConnection>&, void*, size_t)>& cb);
    void setWriteCompleteCallback(const std::function<void(const std::shared_ptr<TcpConnection>&)>& cb);
    
    // 设置线程初始化回调
    void setThreadInitCallback(const ThreadInitCallback& cb) {
        threadInitCallback_ = cb;
    }
    
    // 开始监听
    void start();
    
    // 停止服务器
    void stop();
    
    // 设置线程数量
    void setThreadNum(int numThreads);
    
    // 获取服务器信息
    const std::string& name() const { return name_; }
    const std::string& listenAddress() const { return listenAddr_; }
    uint16_t listenPort() const { return listenPort_; }
    
    // 获取线程池
    EventLoopThreadPool* threadPool() { return threadPool_.get(); }

private:
    // 新连接处理
    void newConnection(int sockfd, const std::string& localAddr, const std::string& peerAddr);
    
    // 连接移除处理
    void removeConnection(const std::shared_ptr<TcpConnection>& conn);
    void removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);
    
    EventLoop* loop_;
    const std::string name_;
    const std::string listenAddr_;
    const uint16_t listenPort_;
    
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<EventLoopThreadPool> threadPool_;
    
    std::atomic<bool> started_;
    int nextConnId_;
    std::map<std::string, std::shared_ptr<TcpConnection>> connections_;
    
    std::function<void(const std::shared_ptr<TcpConnection>&)> connectionCallback_;
    std::function<void(const std::shared_ptr<TcpConnection>&, void*, size_t)> messageCallback_;
    std::function<void(const std::shared_ptr<TcpConnection>&)> writeCompleteCallback_;
    ThreadInitCallback threadInitCallback_;
    
    static std::atomic<int> s_numCreated_;
};
