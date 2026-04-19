#include "Acceptor.h"
#include "EventLoop.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

Acceptor::Acceptor(EventLoop* loop, const std::string& listenAddr, uint16_t listenPort, bool reuseport)
    : loop_(loop),
      acceptSocket_(new Socket(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP))),
      acceptChannel_(new Channel(loop, acceptSocket_->fd())),
      newConnectionCallback_(),
      listening_(false),
      idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)),
      listenAddr_(listenAddr),
      listenPort_(listenPort) {
    
    acceptSocket_->setReuseAddr(true);
    acceptSocket_->setReusePort(reuseport);
    acceptSocket_->bindAddress(listenAddr, listenPort);
    
    acceptChannel_->setReadCallback(
        std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor() {
    acceptChannel_->disableAll();
    acceptChannel_->remove();
    ::close(idleFd_);
}

void Acceptor::listen() {
    loop_->assertInLoopThread();
    listening_ = true;
    acceptSocket_->listen();
    acceptChannel_->enableReading();
    
    LOG_INFO("Acceptor 开始监听 {}:{} - fd {}", listenAddr_, listenPort_, acceptSocket_->fd());
}

void Acceptor::handleRead() {
    loop_->assertInLoopThread();
    
    // 轮询接受所有连接
    while (true) {
        struct sockaddr_in peerAddr {};
        int connfd = acceptSocket_->accept(&peerAddr);
        if (connfd >= 0) {
            // 获取对端地址
            char buf[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &peerAddr.sin_addr, buf, sizeof(buf));
            std::string peerAddrStr(buf);
            uint16_t peerPort = ntohs(peerAddr.sin_port);
            
            LOG_DEBUG("新连接来自 {}:{}", peerAddrStr, peerPort);
            
            if (newConnectionCallback_) {
                newConnectionCallback_(connfd, listenAddr_ + ":" + std::to_string(listenPort_), 
                                   peerAddrStr + ":" + std::to_string(peerPort));
            } else {
                ::close(connfd);
            }
        } else {
            int savedErrno = errno;
            if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
                // 没有更多连接了
                break;
            } else if (savedErrno == EMFILE) {
                // 文件描述符耗尽，临时关闭 idleFd 来接受连接然后立即关闭
                ::close(idleFd_);
                idleFd_ = ::accept(acceptSocket_->fd(), nullptr, nullptr);
                ::close(idleFd_);
                idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
                LOG_ERROR("Acceptor::handleRead() EMFILE，临时关闭 idleFd 处理连接");
                break;
            } else {
                LOG_ERROR("Acceptor::handleRead() 接收连接失败: {}", strerror(savedErrno));
                break;
            }
        }
    }
}

void Acceptor::stop() {
    if (listening_) {
        LOG_INFO("Acceptor::stop [{}:{}] - 正在停止接收器", listenAddr_, listenPort_);
        
        listening_ = false;
        
        // 禁用 Channel
        if (acceptChannel_) {
            acceptChannel_->disableAll();
            acceptChannel_->remove();
        }
        
        // 关闭监听 socket
        if (acceptSocket_) {
            acceptSocket_->shutdown();
        }
        
        LOG_INFO("Acceptor::stop [{}:{}] - 接收器已停止", listenAddr_, listenPort_);
    }
}
