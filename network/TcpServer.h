#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <atomic>
#include "logger.h"

class EventLoop;
class Acceptor;
class EventLoopThreadPool;

#include "TcpConnection.h"

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

    /// 0 表示不限制并发连接数
    void setMaxConnections(int max_connections);
    void setConnectionSocketTuning(const TcpSocketTuningOptions& tuning);
    
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
    
    EventLoop* loop_;  // 事件循环  
    const std::string name_;
    const std::string listenAddr_;  // 监听地址
    const uint16_t listenPort_;  // 监听端口
    
    std::unique_ptr<Acceptor> acceptor_;  // 接受器
    std::unique_ptr<EventLoopThreadPool> threadPool_;  // 事件循环线程池
      
    std::atomic<bool> started_;  // 是否启动
    int nextConnId_;  // 下一个连接 ID
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> connections_;  // 连接映射
    
    std::function<void(const std::shared_ptr<TcpConnection>&)> connectionCallback_;  // 连接回调
    std::function<void(const std::shared_ptr<TcpConnection>&, void*, size_t)> messageCallback_;  // 消息回调
    std::function<void(const std::shared_ptr<TcpConnection>&)> writeCompleteCallback_;  // 写完成回调
    ThreadInitCallback threadInitCallback_;  // 线程初始化回调

    int max_connections_{0};
    TcpSocketTuningOptions conn_tuning_{};
    
    static std::atomic<int> s_numCreated_;  // 创建的连接数
};
