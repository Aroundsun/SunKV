#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../common/Config.h"
#include "../../network/logger.h"
#include "../../protocol/RESPParser.h"
#include "../../server/Server.h"

namespace server_test {

inline void setNonBlocking(int fd, bool nonBlocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return;
    if (nonBlocking) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    } else {
        (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
}

template <class Pred>
inline bool waitUntil(Pred pred,
                       std::chrono::milliseconds timeout,
                       std::chrono::milliseconds step = std::chrono::milliseconds(10)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(step);
    }
    return pred();
}

inline int pickFreePort() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // let OS allocate

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        ::close(fd);
        return -1;
    }

    const int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

inline int connectToHost(const std::string& host,
                           int port,
                           std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        setNonBlocking(fd, true);

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            ::close(fd);
            return -1;
        }

        int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == 0) {
            setNonBlocking(fd, false);
            return fd;
        }

        if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000; // 50ms
            int sel = ::select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wfds)) {
                int so_error = 0;
                socklen_t slen = sizeof(so_error);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &slen) == 0 && so_error == 0) {
                    setNonBlocking(fd, false);
                    return fd;
                }
            }
        }

        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return -1;
}

inline bool sendAll(int fd, std::string_view data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return false;
    }
    return true;
}

inline std::string respBulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

inline std::string respArrayBulk(const std::vector<std::string>& elems) {
    std::string out = "*" + std::to_string(elems.size()) + "\r\n";
    for (const auto& e : elems) out += respBulk(e);
    return out;
}

struct RespStreamReader {
    explicit RespStreamReader(int socketFd) : fd(socketFd) {}

    // 返回“恰好一个”RESP 值的原始字节串；解析前会读取必要数据。
    std::string recvOneRESP(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            RESPParser parser;
            ParseResult r = parser.parse(std::string_view(buf));
            if (r.success && r.complete && r.value) {
                const size_t consumed = r.processed_bytes;
                std::string out = buf.substr(0, consumed);
                buf.erase(0, consumed);
                return out;
            }

            // 还不完整：尝试继续读取
            char tmp[8192];
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000; // 50ms

            int sel = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel < 0) {
                if (errno == EINTR) continue;
                return {};
            }
            if (sel == 0) {
                continue; // timeout slice, re-evaluate parser
            }

            ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
            if (n > 0) {
                buf.append(tmp, tmp + n);
                continue;
            }
            if (n == 0) {
                // peer closed: 如果缓冲里已经是一个完整 RESP，则也应当返回它
                {
                    RESPParser parser2;
                    ParseResult r2 = parser2.parse(std::string_view(buf));
                    if (r2.success && r2.complete && r2.value) {
                        const size_t consumed2 = r2.processed_bytes;
                        std::string out = buf.substr(0, consumed2);
                        buf.erase(0, consumed2);
                        return out;
                    }
                }
                return {};
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EINTR) continue;
            return {};
        }
        return {};
    }

    int fd;
    std::string buf;
};

struct ServerFixture {
    explicit ServerFixture(const std::string& testName)
        : host("127.0.0.1"),
          port(pickFreePort()),
          baseDir("/tmp/sunkv_test_" + testName + "_" + std::to_string(::getpid())),
          cfg(Config::getInstance()) {
        assert(port > 0);

        cfg.enable_console_log = false;
        cfg.enable_periodic_stats_log = false;
        cfg.enable_wal = false;         // reduce persistence overhead for unit-like tests
        cfg.enable_snapshot = false;    // reduce persistence overhead for unit-like tests
        cfg.ttl_cleanup_interval_seconds = 1;
        cfg.stats_log_interval_seconds = 1;

        cfg.host = host;
        cfg.port = port;

        cfg.data_dir = baseDir + "/data";
        cfg.wal_dir = baseDir + "/wal";
        cfg.snapshot_dir = baseDir + "/snapshot";
        cfg.log_file = baseDir + "/logs/sunkv.log";
    }

    Server::ServerStats getStats() const {
        Server* s = serverPtr.load(std::memory_order_acquire);
        if (!s) return Server::ServerStats{0, 0, 0, 0, 0};
        return s->getStats();
    }

    void start() {
        // 静态初始化日志级别（可选）
        Logger::instance().setConsoleEnabled(false);

        startThreadExited.store(false);
        startThreadResult.store(false);
        serverThread = std::thread([this]() {
            Server* s = new Server(cfg);
            serverPtr.store(s, std::memory_order_release);

            bool ok = s->start();
            startThreadResult.store(ok);
            startThreadExited.store(true);

            delete s;
            serverPtr.store(nullptr, std::memory_order_release);
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            int fd = connectToHost(host, port, std::chrono::milliseconds(100));
            if (fd >= 0) {
                ::close(fd);
                (void)waitUntil([&]() {
                    return serverPtr.load(std::memory_order_acquire) != nullptr;
                }, std::chrono::seconds(2));
                return; // 以 connect 成功作为 server 就绪条件
            }

            if (startThreadExited.load(std::memory_order_acquire)) {
                // server.start() 提前失败（或立即返回）
                serverThread.join();
                assert(startThreadResult.load(std::memory_order_acquire) && "server.start() failed");
            }
        }

        {
            Server* s = serverPtr.load(std::memory_order_acquire);
            if (s) {
                s->setStopping();
                s->stopMainLoop();
            }
            if (serverThread.joinable()) serverThread.join();
            const bool exited = startThreadExited.load(std::memory_order_acquire);
            const bool ok = startThreadResult.load(std::memory_order_acquire);
            (void)exited;
            (void)ok;
            assert(false && "failed to connect to server");
        }
    }

    void stop() {
        // 不直接调用 server.stop()（它会触发 TcpServer::stop/事件循环线程断言）。
        // 而是只设置 stopping 标志并唤醒主 loop，让 server.start() 线程自己进入 stop()。
        Server* s = serverPtr.load(std::memory_order_acquire);
        if (s) {
            // 等一下，确保连接关闭回调链路处理完（避免 deferred 的 connectDestroyed/remove 没来得及执行）。
            (void)waitUntil([&]() {
                return getStats().current_connections == 0;
            }, std::chrono::seconds(3));

            s->setStopping();
            s->stopMainLoop();
        }

        if (serverThread.joinable()) serverThread.join();
        serverRunning = false;
    }

    ~ServerFixture() {
        if (serverRunning) {
            // best-effort
            Server* s = serverPtr.load(std::memory_order_acquire);
            if (s) {
                (void)waitUntil([&]() {
                    return getStats().current_connections == 0;
                }, std::chrono::seconds(3));

                s->setStopping();
                s->stopMainLoop();
            }
            if (serverThread.joinable()) serverThread.join();
        }
    }

    std::string host;
    int port;
    std::string baseDir;
    Config cfg;
    std::atomic<Server*> serverPtr{nullptr};

    std::thread serverThread;
    std::atomic<bool> startThreadExited{false};
    std::atomic<bool> startThreadResult{false};
    bool serverRunning{true};
};

} // namespace server_test

