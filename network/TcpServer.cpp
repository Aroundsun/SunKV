#include "TcpServer.h"
#include "EventLoop.h"
#include "Acceptor.h"
#include "TcpConnection.h"
#include "EventLoopThreadPool.h"
#include <unistd.h>
#include <stdio.h>

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

std::atomic<int> TcpServer::s_numCreated_{0};

TcpServer::TcpServer(EventLoop* loop, const std::string& name, const std::string& listenAddr, uint16_t listenPort)
    : loop_(loop),
      name_(name),
      listenAddr_(listenAddr),
      listenPort_(listenPort),
      acceptor_(new Acceptor(loop, listenAddr, listenPort)),
      threadPool_(new EventLoopThreadPool(loop, name_)),
      started_(false),
      nextConnId_(1) {
    
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

TcpServer::~TcpServer() {
    // 不在这里调用 assertInLoopThread，因为 TcpServer 可能在不同线程中析构
    // 将清理工作移到事件循环线程中执行
    // 如果 EventLoop 还在运行，就清理连接；如果已经在析构，就跳过
    if (loop_ && !loop_->isDestructing()) {
        // 直接清理连接，避免跨线程调用
        for (auto& conn : connections_) {
            TcpConnectionPtr connPtr = conn.second;
            conn.second.reset();
            // 不调用 connectDestroyed，避免跨线程问题
        }
        connections_.clear();
    }
}

void TcpServer::setConnectionCallback(const std::function<void(const std::shared_ptr<TcpConnection>&)>& cb) {
    connectionCallback_ = cb;
}

void TcpServer::setMessageCallback(const std::function<void(const std::shared_ptr<TcpConnection>&, void*, size_t)>& cb) {
    messageCallback_ = cb;
}

void TcpServer::setWriteCompleteCallback(const std::function<void(const std::shared_ptr<TcpConnection>&)>& cb) {
    writeCompleteCallback_ = cb;
}

void TcpServer::setThreadNum(int numThreads) {
    assert(0 <= numThreads);
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start() {
    if (!started_.exchange(true)) {
        if (threadPool_) {
            threadPool_->setThreadInitCallback(threadInitCallback_);
            threadPool_->start();
        }
        
        loop_->runInLoop(
            std::bind(&Acceptor::listen, acceptor_.get()));
        
        LOG_INFO("TcpServer {} started on {}:{}", name_, listenAddr_, listenPort_);
    }
}

void TcpServer::newConnection(int sockfd, const std::string& localAddr, const std::string& peerAddr) {
    loop_->assertInLoopThread();
    
    char buf[64];
    snprintf(buf, sizeof(buf), "#%d", nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;
    
    LOG_INFO("TcpServer::newConnection [{}] - new connection [{}] from {} to {}", 
             name_, connName, peerAddr, localAddr);
    
    // 从线程池中选择一个 EventLoop
    EventLoop* ioLoop = threadPool_->getNextLoop();
    TcpConnectionPtr conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
    
    connections_[connName] = conn;
    
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
    
    ioLoop->runInLoop(
        std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const std::shared_ptr<TcpConnection>& conn) {
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn) {
    loop_->assertInLoopThread();
    
    LOG_INFO("TcpServer::removeConnectionInLoop [{}] - connection [{}]", name_, conn->name());
    
    size_t n = connections_.erase(conn->name());
    assert(n == 1);
    
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
}
