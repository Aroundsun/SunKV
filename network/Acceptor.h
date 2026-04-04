#pragma once

#include <functional>
#include <memory>
#include "Socket.h"
#include "Channel.h"
#include "logger.h"

class EventLoop;

// 连接接受器类
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int sockfd, const std::string& localAddr, const std::string& peerAddr)>;
    
    Acceptor(EventLoop* loop, const std::string& listenAddr, uint16_t listenPort, bool reuseport = true);
    ~Acceptor();
    
    // 禁止拷贝和赋值
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    
    // 开始监听
    void listen();
    
    // 停止监听
    void stop();
    
    // 是否正在监听
    bool listening() const { return listening_; }
    
    // 设置新连接回调
    void setNewConnectionCallback(const NewConnectionCallback& cb) {
        newConnectionCallback_ = cb;
    }
    
    // 获取监听地址
    std::string listenAddress() const { return listenAddr_; }
    uint16_t listenPort() const { return listenPort_; }

private:
    // 处理读事件（新连接）
    void handleRead();
    
    EventLoop* loop_;
    std::unique_ptr<Socket> acceptSocket_;
    std::unique_ptr<Channel> acceptChannel_;
    NewConnectionCallback newConnectionCallback_;
    bool listening_;
    int idleFd_;
    
    const std::string listenAddr_;
    const uint16_t listenPort_;
};
