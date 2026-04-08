#include "TcpConnection.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Socket.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>

std::atomic<int64_t> TcpConnection::s_numCreated_{0};

TcpConnection::TcpConnection(EventLoop* loop, const std::string& name, int sockfd,
                           const std::string& localAddr, const std::string& peerAddr)
    : loop_(loop),
      name_(name),
      state_(TcpConnectionState::kConnecting),
      reading_(true),
      socket_(new Socket(sockfd)),
      channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr),
      peerAddr_(peerAddr),
      inputBuffer_(),
      outputBuffer_() {
    
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this));
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this));
    
    LOG_DEBUG("TcpConnection 已创建 {} - {}", localAddr_, peerAddr_);
}

TcpConnection::~TcpConnection() {
    LOG_DEBUG("TcpConnection 已销毁 {} - {}", localAddr_, peerAddr_);
    assert(state_ == TcpConnectionState::kDisconnected);
}

void TcpConnection::connectEstablished() {
    loop_->assertInLoopThread();
    setState(TcpConnectionState::kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();
    
    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed() {
    loop_->assertInLoopThread();
    if (state_ == TcpConnectionState::kConnected) {
        setState(TcpConnectionState::kDisconnected);
        channel_->disableAll();
        
        if (connectionCallback_) {
            connectionCallback_(shared_from_this());
        }
    }
    
    channel_->remove();
}

void TcpConnection::send(const std::string& message) {
    if (state_ == TcpConnectionState::kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(message);
        } else {
            loop_->runInLoop(
                [this, message]() { sendInLoop(message); });
        }
    }
}

void TcpConnection::send(const void* data, size_t len) {
    if (state_ == TcpConnectionState::kConnected) {
        if (loop_->isInLoopThread()) {
            sendInLoop(data, len);
        } else {
            std::string message(static_cast<const char*>(data), len);
            loop_->runInLoop(
                [this, message]() { sendInLoop(message); });
        }
    }
}

void TcpConnection::send(Buffer* buffer) {
    if (state_ == TcpConnectionState::kConnected) {
        if (buffer->readableBytes() > 0) {
            if (loop_->isInLoopThread()) {
                sendInLoop(buffer->peek(), buffer->readableBytes());
                buffer->retrieveAll();
            } else {
                std::string message(buffer->retrieveAllAsString());
                loop_->runInLoop(
                    [this, message]() { sendInLoop(message); });
            }
        }
    }
}

void TcpConnection::sendInLoop(const std::string& message) {
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len) {
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    
    if (state_ == TcpConnectionState::kDisconnected) {
        LOG_WARN("TcpConnection::sendInLoop() 连接已断开，放弃写入");
        return;
    }
    
    // 如果输出缓冲区没有数据，尝试直接写入
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        nwrote = ::write(socket_->fd(), data, remaining);
        if (nwrote >= 0) {
            remaining -= nwrote;
            if (remaining == 0 && writeCompleteCallback_) {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        } else {
            nwrote = 0;
            if (errno != EWOULDBLOCK) {
                LOG_ERROR("TcpConnection::sendInLoop() 写入错误: {}", strerror(errno));
                if (errno == EPIPE || errno == ECONNRESET) {
                    faultError = true;
                }
            }
        }
    }
    
    // 如果还有数据未发送，放入输出缓冲区
    if (!faultError && remaining > 0) {
        // append 方法会自动处理空间不足的情况
        outputBuffer_.append(static_cast<const char*>(data) + nwrote, remaining);
        
        if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
    }
}

void TcpConnection::shutdown() {
    if (state_ == TcpConnectionState::kConnected) {
        setState(TcpConnectionState::kDisconnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop() {
    loop_->assertInLoopThread();
    if (!channel_->isWriting()) {
        socket_->shutdown();
    }
}

void TcpConnection::forceClose() {
    if (state_ == TcpConnectionState::kConnected || state_ == TcpConnectionState::kDisconnecting) {
        setState(TcpConnectionState::kDisconnecting);
        loop_->queueInLoop(
            std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

void TcpConnection::forceCloseInLoop() {
    loop_->assertInLoopThread();
    if (state_ == TcpConnectionState::kConnected || state_ == TcpConnectionState::kDisconnecting) {
        handleClose();
    }
}

void TcpConnection::setTcpNoDelay(bool on) {
    socket_->setTcpNoDelay(on);
}

void TcpConnection::setKeepAlive(bool on) {
    socket_->setKeepAlive(on);
}

void TcpConnection::startRead() {
    loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop() {
    loop_->assertInLoopThread();
    if (!reading_ || !channel_->isReading()) {
        channel_->enableReading();
        reading_ = true;
    }
}

void TcpConnection::stopRead() {
    loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop() {
    loop_->assertInLoopThread();
    if (reading_ || channel_->isReading()) {
        channel_->disableReading();
        reading_ = false;
    }
}

void TcpConnection::handleRead() {
    loop_->assertInLoopThread();
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(socket_->fd(), &savedErrno);
    
    if (n > 0) {
        if (messageCallback_) {
            messageCallback_(shared_from_this(), &inputBuffer_, n);
        }
    } else if (n == 0) {
        handleClose();
    } else {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead() 读取错误: {}", strerror(errno));
        handleError();
    }
}

void TcpConnection::handleWrite() {
    loop_->assertInLoopThread();
    if (channel_->isWriting()) {
        ssize_t n = ::write(socket_->fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
        if (n > 0) {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0) {
                channel_->disableWriting();
                if (writeCompleteCallback_) {
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == TcpConnectionState::kDisconnecting) {
                    shutdownInLoop();
                }
            }
        } else {
            LOG_ERROR("TcpConnection::handleWrite() 写入错误: {}", strerror(errno));
        }
    } else {
        LOG_DEBUG("TcpConnection::handleWrite() 连接已关闭，不再写入");
    }
}

void TcpConnection::handleClose() {
    loop_->assertInLoopThread();
    LOG_DEBUG("TcpConnection::handleClose() 状态 = {}", static_cast<int>(state_.load()));
    
    setState(TcpConnectionState::kDisconnected);
    channel_->disableAll();
    
    TcpConnectionPtr guardThis(shared_from_this());
    if (connectionCallback_) {
        connectionCallback_(guardThis);
    }
    
    if (closeCallback_) {
        closeCallback_(guardThis);
    }
}

void TcpConnection::handleError() {
    int err = socket_->getSocketError();
    LOG_ERROR("TcpConnection::handleError() - {} - SO_ERROR = {} : {}", 
              name_, err, strerror(err));
}
