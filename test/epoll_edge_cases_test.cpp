#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cerrno>
 
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
 
#include "network/logger.h"
#include "network/TcpServer.h"
#include "network/TcpConnection.h"
#include "network/EventLoop.h"
 
namespace {
 
int connectTcp(const char* ip, uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        ::close(fd);
        errno = EINVAL;
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}
 
bool waitUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}
 
} // namespace
 
int main() {
    Logger::instance().setLevel(spdlog::level::info);
    LOG_INFO("Epoll edge cases test starting...");
 
    std::cout << "=== Epoll Edge Cases Test ===" << std::endl;
 
    constexpr const char* kHost = "127.0.0.1";
    constexpr uint16_t kPort = 18080; // 与现有测试端口分离，降低冲突概率
 
    std::atomic<int> up{0};
    std::atomic<int> down{0};
 
    EventLoop* loop = nullptr;
 
    std::thread loopThread([&]() {
        loop = new EventLoop();
        auto* server = new TcpServer(loop, "EdgeCaseServer", kHost, kPort);
        server->setThreadNum(1);
 
        server->setConnectionCallback([&](const std::shared_ptr<TcpConnection>& conn) {
            if (conn->connected()) {
                ++up;
                LOG_INFO("conn up: {}", conn->peerAddress());
            } else {
                ++down;
                LOG_INFO("conn down: {}", conn->peerAddress());
            }
        });
 
        // 读到数据无关紧要；重点是连接边界事件能稳定触发清理路径
        server->setMessageCallback([](const std::shared_ptr<TcpConnection>& /*conn*/, void* /*data*/, size_t /*len*/) {});
 
        server->start();
 
        // 由主线程触发退出；这里仅运行 loop
        loop->loop();
 
        delete server;
        delete loop;
        loop = nullptr;
    });
 
    // 等待 server 启动
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
 
    bool ok = true;
 
    // Case 1: 正常 close (FIN)
    {
        int fd = connectTcp(kHost, kPort);
        if (fd < 0) {
            std::cerr << "FAIL: connect (FIN) errno=" << errno << " " << std::strerror(errno) << std::endl;
            ok = false;
        } else {
            ::close(fd);
            if (!waitUntil([&]() { return down.load() >= 1; }, std::chrono::seconds(2))) {
                std::cerr << "FAIL: FIN close not observed" << std::endl;
                ok = false;
            }
        }
    }
 
    // Case 2: 半关闭 shutdown(SHUT_WR) + close
    {
        int fd = connectTcp(kHost, kPort);
        if (fd < 0) {
            std::cerr << "FAIL: connect (half-close) errno=" << errno << " " << std::strerror(errno) << std::endl;
            ok = false;
        } else {
            ::shutdown(fd, SHUT_WR);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            ::close(fd);
            if (!waitUntil([&]() { return down.load() >= 2; }, std::chrono::seconds(2))) {
                std::cerr << "FAIL: half-close not observed" << std::endl;
                ok = false;
            }
        }
    }
 
    // Case 3: RST（SO_LINGER=0）
    {
        int fd = connectTcp(kHost, kPort);
        if (fd < 0) {
            std::cerr << "FAIL: connect (RST) errno=" << errno << " " << std::strerror(errno) << std::endl;
            ok = false;
        } else {
            linger lin{};
            lin.l_onoff = 1;
            lin.l_linger = 0;
            ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            ::close(fd);
            if (!waitUntil([&]() { return down.load() >= 3; }, std::chrono::seconds(3))) {
                std::cerr << "FAIL: RST close not observed" << std::endl;
                ok = false;
            }
        }
    }
 
    // sanity: up/down 数量应一致
    if (up.load() != down.load()) {
        std::cerr << "FAIL: up/down mismatch: up=" << up.load() << " down=" << down.load() << std::endl;
        ok = false;
    }
 
    // stop loop
    if (loop) {
        loop->runInLoop([&]() { loop->quit(); });
    }
    loopThread.join();
 
    if (ok) {
        std::cout << " Epoll edge cases test PASSED!" << std::endl;
        return 0;
    }
    std::cout << " Epoll edge cases test FAILED!" << std::endl;
    return 1;
}
