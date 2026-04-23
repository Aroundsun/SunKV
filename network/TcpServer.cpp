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
    if (loop_ && !loop_->isDestructing()) {
        // 在事件循环线程中清理连接
        loop_->runInLoop([this]() {
            for (auto& conn : connections_) {
                TcpConnectionPtr connPtr = conn.second;
                conn.second.reset();
                if (connPtr && connPtr->getLoop()) {
                    connPtr->getLoop()->runInLoop(
                        std::bind(&TcpConnection::connectDestroyed, connPtr));
                }
            }
            connections_.clear();
        });
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

void TcpServer::setMaxConnections(int max_connections) {
    max_connections_ = max_connections < 0 ? 0 : max_connections;
}

void TcpServer::setConnectionSocketTuning(const TcpSocketTuningOptions& tuning) {
    conn_tuning_ = tuning;
}

void TcpServer::start() {
    if (!started_.exchange(true)) {
        if (threadPool_) {
            threadPool_->setThreadInitCallback(threadInitCallback_);
            threadPool_->start();
        }
        
        loop_->runInLoop(
            std::bind(&Acceptor::listen, acceptor_.get()));
        
        LOG_INFO("TcpServer {} 已启动，监听 {}:{}", name_, listenAddr_, listenPort_);
    }
}

void TcpServer::newConnection(int sockfd, const std::string& localAddr, const std::string& peerAddr) {
    loop_->assertInLoopThread();

    if (max_connections_ > 0 && connections_.size() >= static_cast<size_t>(max_connections_)) {
        ::close(sockfd);
        LOG_WARN("TcpServer::newConnection [{}] 拒绝新连接：已达上限 max_connections={}", name_, max_connections_);
        return;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "#%d", nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;
    
    LOG_DEBUG("TcpServer::newConnection [{}] - 新连接 [{}]，来自 {} -> {}", 
              name_, connName, peerAddr, localAddr);
    
    // 从线程池中选择一个 EventLoop
    EventLoop* ioLoop = threadPool_->getNextLoop();
    TcpConnectionPtr conn = std::make_shared<TcpConnection>(ioLoop, connName, sockfd, localAddr, peerAddr);
    conn->setSocketTuningOptions(conn_tuning_);
    
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
// 在事件循环线程中删除连接
void TcpServer::removeConnectionInLoop(const std::shared_ptr<TcpConnection>& conn) {
    loop_->assertInLoopThread();
    
    LOG_DEBUG("TcpServer::removeConnectionInLoop [{}] - 连接 [{}]", name_, conn->name());
    
    const size_t n = connections_.erase(conn->name());
    if (n == 0) {
        // 停机阶段可能已提前从连接表移除
        LOG_DEBUG("TcpServer::removeConnectionInLoop [{}] - 连接 [{}] 已不在连接表中", name_, conn->name());
    }
    
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn));
}


void TcpServer::stop() {
    if (started_.exchange(false)) {
        LOG_INFO("TcpServer::stop [{}] - 正在停止服务器", name_);
        
        // 停止接受新连接
        if (acceptor_) {
            acceptor_->stop();
        }
        
        // 停机时将连接对象先搬到本地 keepalive，保证 clear 之后对象不会提前析构。
        // 这样既避免生命周期断言，也避免后续回调依赖主循环继续清理连接表。
        std::vector<TcpConnectionPtr> keepalive; 
        keepalive.reserve(connections_.size()); 
        // 将连接对象搬到本地 keepalive
        for (auto& entry : connections_) {
            if (entry.second) {
                keepalive.push_back(entry.second);
            }
        }
        connections_.clear();

        // 异步触发连接关闭，connectDestroyed 将在各自 ioLoop 中完成 Channel::remove。
        for (auto& conn : keepalive) {
            conn->forceClose();
        }
        
        // 停止线程池
        if (threadPool_) {
            threadPool_->stop();
        }

        LOG_INFO("TcpServer::stop [{}] - 服务器已停止", name_);
    }
}
