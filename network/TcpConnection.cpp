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

    if (tuning_.send_buffer_size > 0) {
        socket_->setSendBufferSize(tuning_.send_buffer_size);
    }
    if (tuning_.recv_buffer_size > 0) {
        socket_->setRecvBufferSize(tuning_.recv_buffer_size);
    }
    if (tuning_.tcp_keepalive_idle_seconds > 0) {
        socket_->setKeepAlive(true);
        socket_->setTcpKeepAliveIdleSeconds(tuning_.tcp_keepalive_idle_seconds);
    }
    
    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
}

void TcpConnection::connectDestroyed() {
    loop_->assertInLoopThread();
    // 连接销毁时统一触发连接回调，确保 Poller 上的 channel 已移除后再更新统计。
    // 这样避免 Server 因 current_connections_ 过早到 0 而销毁仍挂在 loop 里的 Channel。
    if (state_ == TcpConnectionState::kConnected) {
        setState(TcpConnectionState::kDisconnected);
    }

    channel_->disableAll();
    channel_->remove();

    if (connectionCallback_) {
        connectionCallback_(shared_from_this());
    }
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

void TcpConnection::beginWriteCoalescing() {
    if (loop_->isInLoopThread()) {
        write_coalescing_ = true;
        return;
    }
    loop_->runInLoop([this]() { write_coalescing_ = true; });
}

void TcpConnection::endWriteCoalescing() {
    auto flush = [this]() {
        write_coalescing_ = false;
        if (state_ != TcpConnectionState::kConnected) {
            return;
        }
        if (outputBuffer_.readableBytes() > 0 && !channel_->isWriting()) {
            channel_->enableWriting();
        }
    };
    if (loop_->isInLoopThread()) {
        flush();
        return;
    }
    loop_->runInLoop(flush);
}

void TcpConnection::sendInLoop(const std::string& message) {
    sendInLoop(message.data(), message.size());
}

void TcpConnection::sendInLoop(const void* data, size_t len) {
    loop_->assertInLoopThread();
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    const char* bytes = static_cast<const char*>(data);

    if (state_ == TcpConnectionState::kDisconnected) {
        LOG_WARN("TcpConnection::sendInLoop() 连接已断开，放弃写入");
        return;
    }

    if (write_coalescing_) {
        if (len > 0) {
            outputBuffer_.append(bytes, len);
            if (!channel_->isWriting()) {
                channel_->enableWriting();
            }
            (void)enforceOutputBackpressure_();
        }
        return;
    }

    // 输出队列已有待发数据时，用 writev 一次尝试写出「旧缓冲 + 本段」，减少一次 append 后再依赖 handleWrite 的路径
    if (outputBuffer_.readableBytes() > 0 && len > 0) {
        size_t buf_len = outputBuffer_.readableBytes();
        struct iovec iov[2];
        iov[0].iov_base = const_cast<char*>(outputBuffer_.peek());
        iov[0].iov_len = buf_len;
        iov[1].iov_base = const_cast<char*>(bytes);
        iov[1].iov_len = len;
        ssize_t n = ::writev(socket_->fd(), iov, 2);
        if (n < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                LOG_ERROR("TcpConnection::sendInLoop() writev 错误: {}", strerror(errno));
                if (errno == EPIPE || errno == ECONNRESET) {
                    faultError = true;
                }
            }
            if (!faultError) {
                outputBuffer_.append(bytes, len);
                if (!channel_->isWriting()) {
                    channel_->enableWriting();
                }
                if (!enforceOutputBackpressure_()) {
                    return;
                }
            }
            return;
        }
        if (n == 0) {
            outputBuffer_.append(bytes, len);
            if (!channel_->isWriting()) {
                channel_->enableWriting();
            }
            if (!enforceOutputBackpressure_()) {
                return;
            }
            return;
        }

        size_t nu = static_cast<size_t>(n);
        if (nu <= buf_len) {
            outputBuffer_.retrieve(nu);
            outputBuffer_.append(bytes, len);
        } else {
            outputBuffer_.retrieve(buf_len);
            size_t sent_new = nu - buf_len;
            if (sent_new < len) {
                outputBuffer_.append(bytes + sent_new, len - sent_new);
            }
        }

        if (outputBuffer_.readableBytes() == 0) {
            channel_->disableWriting();
            if (writeCompleteCallback_) {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
            if (state_ == TcpConnectionState::kDisconnecting) {
                shutdownInLoop();
            }
        } else if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
        if (!enforceOutputBackpressure_()) {
            return;
        }
        return;
    }

    // 输出缓冲区为空：尽量一次 write 直接进内核
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        nwrote = ::write(socket_->fd(), bytes, remaining);
        if (nwrote >= 0) {
            remaining -= static_cast<size_t>(nwrote);
            if (remaining == 0 && writeCompleteCallback_) {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        } else {
            nwrote = 0;
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                LOG_ERROR("TcpConnection::sendInLoop() 写入错误: {}", strerror(errno));
                if (errno == EPIPE || errno == ECONNRESET) {
                    faultError = true;
                }
            }
        }
    }

    if (!faultError && remaining > 0) {
        outputBuffer_.append(bytes + static_cast<size_t>(nwrote), remaining);

        if (!channel_->isWriting()) {
            channel_->enableWriting();
        }
        if (!enforceOutputBackpressure_()) {
            return;
        }
    }
}

bool TcpConnection::enforceOutputBackpressure_() {
    const size_t pending = outputBuffer_.readableBytes();
    if (pending <= kHighWaterMarkBytes_) {
        return true;
    }
    LOG_WARN("TcpConnection 输出缓冲超过高水位，连接={} pending={} bytes，执行强制关闭",
             name_, pending);
    forceCloseInLoop();
    return false;
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
    if (closeCallback_) {
        closeCallback_(guardThis);
    }
}

void TcpConnection::handleError() {
    int err = socket_->getSocketError();
    LOG_ERROR("TcpConnection::handleError() - {} - SO_ERROR = {} : {}", 
              name_, err, strerror(err));
}
