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
    constexpr size_t kPayloadBytes = 2u * 1024u * 1024u;

    std::atomic<int> write_complete{0};
    std::atomic<int> disconnected{0};
    std::atomic<bool> loop_ready{false};
    EventLoop* loop = nullptr;
    std::shared_ptr<TcpConnection> conn;
    static const std::string payload(kPayloadBytes, 'P');

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
        conn = std::make_shared<TcpConnection>(loop, "buffered-send-conn", fds[0], "local", "peer");
        conn->setConnectionCallback([&](const std::shared_ptr<TcpConnection>& c) {
            if (c->connected()) {
                c->send(payload);
            }
        });
        conn->setWriteCompleteCallback([&](const std::shared_ptr<TcpConnection>& c) {
            write_complete.fetch_add(1);
            c->shutdown();
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

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string received;
    received.reserve(payload.size());
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fds[1], buf, sizeof(buf));
        if (n > 0) {
            received.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        std::cerr << "read failed: " << std::strerror(errno) << std::endl;
        ::close(fds[1]);
        if (loop) {
            loop->runInLoop([&]() { loop->quit(); });
        }
        loop_thread.join();
        return 1;
    }

    ::close(fds[1]);
    const bool closed = waitUntil([&]() { return disconnected.load() >= 1; }, std::chrono::seconds(2));
    if (loop) {
        loop->runInLoop([&]() { loop->quit(); });
    }
    loop_thread.join();

    if (received != payload) {
        std::cerr << "payload mismatch received=" << received.size()
                  << " expected=" << payload.size() << std::endl;
        return 1;
    }
    if (write_complete.load() != 1) {
        std::cerr << "expected exactly one write complete callback, got "
                  << write_complete.load() << std::endl;
        return 1;
    }
    if (!closed) {
        std::cerr << "expected graceful disconnect after write completion" << std::endl;
        return 1;
    }

    std::cout << "network tcp buffered send test passed." << std::endl;
    return 0;
}
