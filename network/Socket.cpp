#include "Socket.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <netinet/tcp.h>
#include "logger.h"

Socket::~Socket() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
    }
}

void Socket::bindAddress(const std::string& addr, uint16_t port) {
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (addr.empty() || addr == "0.0.0.0") {
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, addr.c_str(), &serverAddr.sin_addr) <= 0) {
            LOG_ERROR("无效地址: {}", addr);
            return;
        }
    }
    
    if (::bind(sockfd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        LOG_ERROR("绑定失败: {}", strerror(errno));
    }
}

void Socket::listen() {
    if (::listen(sockfd_, SOMAXCONN) < 0) {
        LOG_ERROR("监听失败: {}", strerror(errno));
    }
}

int Socket::accept() {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    
    int connfd = ::accept4(sockfd_, (struct sockaddr*)&clientAddr, 
                         &clientAddrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd < 0) {
        int savedErrno = errno;
        if (savedErrno != EAGAIN && savedErrno != EWOULDBLOCK) {
            LOG_ERROR("接受连接失败: {}", strerror(savedErrno));
        }
    }
    
    return connfd;
}

void Socket::connect(const std::string& addr, uint16_t port) {
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, addr.c_str(), &serverAddr.sin_addr) <= 0) {
        LOG_ERROR("无效地址: {}", addr);
        return;
    }
    
    if (::connect(sockfd_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        LOG_ERROR("连接失败: {}", strerror(errno));
    }
}

void Socket::shutdown() {
    if (::shutdown(sockfd_, SHUT_WR) < 0) {
        LOG_ERROR("关闭写端失败: {}", strerror(errno));
    }
}

void Socket::setReuseAddr(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

void Socket::setTcpNoDelay(bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

std::string Socket::getLocalAddress() const {
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    
    if (::getsockname(sockfd_, (struct sockaddr*)&localAddr, &addrLen) < 0) {
        LOG_ERROR("获取本地地址失败: {}", strerror(errno));
        return "";
    }
    
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &localAddr.sin_addr, buf, sizeof(buf));
    return std::string(buf);
}

uint16_t Socket::getLocalPort() const {
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    
    if (::getsockname(sockfd_, (struct sockaddr*)&localAddr, &addrLen) < 0) {
        LOG_ERROR("获取本地端口失败: {}", strerror(errno));
        return 0;
    }
    
    return ntohs(localAddr.sin_port);
}

std::string Socket::getPeerAddress() const {
    struct sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);
    
    if (::getpeername(sockfd_, (struct sockaddr*)&peerAddr, &addrLen) < 0) {
        LOG_ERROR("获取对端地址失败: {}", strerror(errno));
        return "";
    }
    
    char buf[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &peerAddr.sin_addr, buf, sizeof(buf));
    return std::string(buf);
}

uint16_t Socket::getPeerPort() const {
    struct sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);
    
    if (::getpeername(sockfd_, (struct sockaddr*)&peerAddr, &addrLen) < 0) {
        LOG_ERROR("获取对端端口失败: {}", strerror(errno));
        return 0;
    }
    
    return ntohs(peerAddr.sin_port);
}

int Socket::getSocketError() const {
    int optval;
    socklen_t optlen = sizeof(optval);
    
    if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
        return errno;
    }
    
    return optval;
}
