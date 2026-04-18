#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

#include "network/EventLoop.h"
#include "network/TcpConnection.h"
#include "network/logger.h"

namespace {

bool waitUntil(const std::function<bool()>& pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

void setRecvBuf(int fd, int recv_buf_bytes) {
    if (recv_buf_bytes > 0) {
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_bytes, sizeof(recv_buf_bytes));
    }
}

void setNonBlock(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

}  // namespace

int main() {
    Logger::instance().setLevel(spdlog::level::info);
    constexpr size_t kPayloadBytes = 32u * 1024u * 1024u;

    std::atomic<int> disconnected{0};
    std::atomic<bool> loop_ready{false};
    EventLoop* loop = nullptr;
    std::shared_ptr<TcpConnection> conn;

    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        std::cerr << "socketpair failed: " << std::strerror(errno) << std::endl;
        return 1;
    }
    setNonBlock(fds[0]);
    setNonBlock(fds[1]);
    setRecvBuf(fds[1], 4096);

    std::thread loop_thread([&]() {
        loop = new EventLoop();
        conn = std::make_shared<TcpConnection>(loop, "backpressure-conn", fds[0], "local", "peer");
        conn->setConnectionCallback([&](const std::shared_ptr<TcpConnection>& c) {
            if (c->connected()) {
                static const std::string payload(kPayloadBytes, 'B');
                c->send(payload);
            }
        });
        conn->setCloseCallback([&](const std::shared_ptr<TcpConnection>&) {
            disconnected.fetch_add(1);
            loop->quit();
        });
        loop_ready.store(true);
        conn->connectEstablished();
        loop->loop();
        conn->connectDestroyed();
        conn.reset();
        delete loop;
        loop = nullptr;
    });

    if (!waitUntil([&]() { return loop_ready.load(); }, std::chrono::seconds(2))) {
        std::cerr << "server did not start in time" << std::endl;
        std::abort();
    }

    const bool backpressure_closed = waitUntil(
        [&]() { return disconnected.load() >= 1; }, std::chrono::seconds(5));

    ::close(fds[1]);
    if (loop) {
        loop->runInLoop([&]() { loop->quit(); });
    }
    loop_thread.join();

    if (!backpressure_closed) {
        std::cerr << "expected connection close after output high-water backpressure" << std::endl;
        return 1;
    }
    std::cout << "network tcp backpressure test passed." << std::endl;
    return 0;
}
