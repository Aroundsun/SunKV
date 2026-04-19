#pragma once

#include <string>
#include <cstdint>
#include <netinet/in.h>

// Socket 类，封装 socket 操作
class Socket {
public:
    explicit Socket(int sockfd) : sockfd_(sockfd) {}
    
    // 禁止拷贝和赋值
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    ~Socket();
    
    // 获取 socket 文件描述符
    int fd() const { return sockfd_; }
    
    // 绑定地址
    void bindAddress(const std::string& addr, uint16_t port);
    
    // 监听连接
    void listen();
    
    // 接受连接
    // - 返回新连接 fd；失败返回 -1
    // - 若 out_peer 非空，写入对端 IPv4 地址与端口（sockaddr_in）
    int accept(struct sockaddr_in* out_peer);
    
    // 连接到服务器
    void connect(const std::string& addr, uint16_t port);
    
    // 关闭连接
    void shutdown();
    
    // 设置 socket 选项
    void setReuseAddr(bool on);
    void setReusePort(bool on);
    void setKeepAlive(bool on);
    void setTcpNoDelay(bool on);
    void setSendBufferSize(int bytes);
    void setRecvBufferSize(int bytes);
    /// Linux: TCP_KEEPIDLE；其它平台可能为 no-op
    void setTcpKeepAliveIdleSeconds(int seconds);
    
    // 获取本地地址
    std::string getLocalAddress() const;
    uint16_t getLocalPort() const;
    
    // 获取对端地址
    std::string getPeerAddress() const;
    uint16_t getPeerPort() const;
    
    // 获取 socket 错误
    int getSocketError() const;

private:
    const int sockfd_;
};
