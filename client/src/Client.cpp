#include "client/include/Client.h"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string_view>

#include "RespCodec.h"

namespace sunkv::client {

struct Client::Impl {
    explicit Impl(Client::Options opts) : options(std::move(opts)) {}

    Client::Options options;
    int fd{-1};
    std::string inbuf;
};

namespace {

Result<void> waitFdReady(int fd, bool writable, int timeout_ms) {
    if (timeout_ms < 0) {
        return Result<void>::failure(ErrorCode::InvalidArgument, "negative timeout");
    }
    fd_set rfds;
    fd_set wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (writable) {
        FD_SET(fd, &wfds);
    } else {
        FD_SET(fd, &rfds);
    }

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int rc = ::select(fd + 1, writable ? nullptr : &rfds, writable ? &wfds : nullptr, nullptr, &tv);
    if (rc == 0) {
        return Result<void>::failure(ErrorCode::Timeout, "I/O timeout");
    }
    if (rc < 0) {
        return Result<void>::failure(ErrorCode::IoError, std::string("select failed: ") + std::strerror(errno));
    }
    return Result<void>::success();
}

Result<void> sendAllWithTimeout(int fd, std::string_view request, int write_timeout_ms) {
    size_t sent = 0;
    while (sent < request.size()) {
        auto wait_w = waitFdReady(fd, true, write_timeout_ms);
        if (!wait_w.ok) return wait_w;
        ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return Result<void>::failure(ErrorCode::IoError, std::string("send failed: ") + std::strerror(errno));
    }
    return Result<void>::success();
}

Result<RespValue> recvOneRespWithTimeout(int fd, std::string& inbuf, int read_timeout_ms) {
    for (;;) {
        RespParseResult parsed = parseRespValue(std::string_view(inbuf));
        if (!parsed.success) {
            return Result<RespValue>::failure(ErrorCode::ProtocolError, parsed.error);
        }
        if (parsed.complete) {
            if (parsed.consumed_bytes > 0 && parsed.consumed_bytes <= inbuf.size()) {
                inbuf.erase(0, parsed.consumed_bytes);
            }
            return Result<RespValue>::success(std::move(parsed.value));
        }

        auto wait_r = waitFdReady(fd, false, read_timeout_ms);
        if (!wait_r.ok) return Result<RespValue>::failure(wait_r.error.code, wait_r.error.message);

        char buf[8192];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            inbuf.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            return Result<RespValue>::failure(ErrorCode::IoError, "peer closed connection");
        }
        if (errno == EINTR) {
            continue;
        }
        return Result<RespValue>::failure(ErrorCode::IoError, std::string("recv failed: ") + std::strerror(errno));
    }
}

} // namespace

Client::Client(Options options) : impl_(new Impl(std::move(options))) {}

Client::~Client() {
    close();
    delete impl_;
    impl_ = nullptr;
}

Result<void> Client::connect() {
    if (isConnected()) {
        return Result<void>::success();
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return Result<void>::failure(ErrorCode::ConnectFailed, std::string("socket failed: ") + std::strerror(errno));
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(impl_->options.port);
    if (::inet_pton(AF_INET, impl_->options.host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::InvalidArgument, "invalid IPv4 address");
    }

    const int old_flags = ::fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::IoError, std::string("fcntl(F_GETFL) failed: ") + std::strerror(errno));
    }
    if (::fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::IoError, std::string("fcntl(F_SETFL) failed: ") + std::strerror(errno));
    }

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::ConnectFailed, std::string("connect failed: ") + std::strerror(errno));
    }
    if (rc < 0) {
        auto wait = waitFdReady(fd, true, impl_->options.connect_timeout_ms);
        if (!wait.ok) {
            ::close(fd);
            return wait;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
            ::close(fd);
            return Result<void>::failure(ErrorCode::ConnectFailed, std::string("getsockopt(SO_ERROR) failed: ") + std::strerror(errno));
        }
        if (so_error != 0) {
            ::close(fd);
            return Result<void>::failure(ErrorCode::ConnectFailed, std::string("connect failed: ") + std::strerror(so_error));
        }
    }

    if (::fcntl(fd, F_SETFL, old_flags) < 0) {
        ::close(fd);
        return Result<void>::failure(ErrorCode::IoError, std::string("restore blocking mode failed: ") + std::strerror(errno));
    }

    impl_->fd = fd;
    impl_->inbuf.clear();
    return Result<void>::success();
}

void Client::close() {
    if (impl_ && impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
    if (impl_) impl_->inbuf.clear();
}

bool Client::isConnected() const {
    return impl_ && impl_->fd >= 0;
}

Result<RespValue> Client::command(const std::vector<std::string>& args) {
    if (!isConnected()) {
        return Result<RespValue>::failure(ErrorCode::NotConnected, "not connected");
    }
    if (args.empty()) {
        return Result<RespValue>::failure(ErrorCode::InvalidArgument, "empty command args");
    }

    const std::string request = encodeRespArrayCommand(args);
    auto sw = sendAllWithTimeout(impl_->fd, request, impl_->options.write_timeout_ms);
    if (!sw.ok) return Result<RespValue>::failure(sw.error.code, sw.error.message);
    auto rr = recvOneRespWithTimeout(impl_->fd, impl_->inbuf, impl_->options.read_timeout_ms);
    if (!rr.ok) {
        if (rr.error.code == ErrorCode::IoError) close();
        return rr;
    }
    if (rr.value.type == RespType::Error) {
        return Result<RespValue>::failure(ErrorCode::ServerError, rr.value.str);
    }
    return rr;
}

Result<std::vector<RespValue>> Client::pipeline(const std::vector<std::vector<std::string>>& commands) {
    return pipeline(commands, PipelineOptions{});
}

Result<std::vector<RespValue>> Client::pipeline(const std::vector<std::vector<std::string>>& commands,
                                                const PipelineOptions& options) {
    if (!isConnected()) {
        return Result<std::vector<RespValue>>::failure(ErrorCode::NotConnected, "not connected");
    }
    if (commands.empty()) {
        return Result<std::vector<RespValue>>::failure(ErrorCode::InvalidArgument, "empty pipeline commands");
    }
    std::string request;
    for (const auto& cmd : commands) {
        if (cmd.empty()) {
            return Result<std::vector<RespValue>>::failure(ErrorCode::InvalidArgument, "pipeline contains empty command");
        }
        request += encodeRespArrayCommand(cmd);
    }
    auto sw = sendAllWithTimeout(impl_->fd, request, impl_->options.write_timeout_ms);
    if (!sw.ok) return Result<std::vector<RespValue>>::failure(sw.error.code, sw.error.message);

    std::vector<RespValue> out;
    out.reserve(commands.size());
    for (size_t i = 0; i < commands.size(); ++i) {
        auto rr = recvOneRespWithTimeout(impl_->fd, impl_->inbuf, impl_->options.read_timeout_ms);
        if (!rr.ok) {
            if (rr.error.code == ErrorCode::IoError) close();
            return Result<std::vector<RespValue>>::failure(rr.error.code, rr.error.message);
        }
        if (options.fail_on_server_error && rr.value.type == RespType::Error) {
            if (options.close_on_server_error) {
                close();
            }
            return Result<std::vector<RespValue>>::failure(
                ErrorCode::ServerError,
                "pipeline command[" + std::to_string(i) + "] failed: " + rr.value.str);
        }
        out.push_back(std::move(rr.value));
    }
    return Result<std::vector<RespValue>>::success(std::move(out));
}

Result<std::string> Client::ping() {
    auto r = command({"PING"});
    if (!r.ok) return Result<std::string>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString || r.value.type == RespType::BulkString) {
        return Result<std::string>::success(r.value.str);
    }
    return Result<std::string>::failure(ErrorCode::InvalidResponse, "PING expected string response");
}

Result<std::optional<std::string>> Client::get(const std::string& key) {
    auto r = command({"GET", key});
    if (!r.ok) return Result<std::optional<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::NullBulkString) {
        return Result<std::optional<std::string>>::success(std::nullopt);
    }
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::optional<std::string>>::success(r.value.str);
    }
    return Result<std::optional<std::string>>::failure(ErrorCode::InvalidResponse, "GET expected bulk string response");
}

Result<void> Client::set(const std::string& key, const std::string& value) {
    auto r = command({"SET", key, value});
    if (!r.ok) return Result<void>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString && r.value.str == "OK") {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::InvalidResponse, "SET expected +OK");
}

Result<int64_t> Client::del(const std::string& key) {
    auto r = command({"DEL", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "DEL expected integer response");
}

Result<int64_t> Client::exists(const std::vector<std::string>& keys) {
    if (keys.empty()) {
        return Result<int64_t>::failure(ErrorCode::InvalidArgument, "EXISTS requires at least one key");
    }
    std::vector<std::string> args{"EXISTS"};
    args.insert(args.end(), keys.begin(), keys.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "EXISTS expected integer response");
}

Result<std::vector<std::string>> Client::keys() {
    auto r = command({"KEYS", "*"});
    if (!r.ok) return Result<std::vector<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type != RespType::Array) {
        return Result<std::vector<std::string>>::failure(ErrorCode::InvalidResponse, "KEYS expected array response");
    }
    std::vector<std::string> out;
    out.reserve(r.value.array.size());
    for (const auto& v : r.value.array) {
        if (v.type != RespType::BulkString && v.type != RespType::SimpleString) {
            return Result<std::vector<std::string>>::failure(ErrorCode::InvalidResponse, "KEYS array element must be string");
        }
        out.push_back(v.str);
    }
    return Result<std::vector<std::string>>::success(std::move(out));
}

Result<int64_t> Client::dbsize() {
    auto r = command({"DBSIZE"});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "DBSIZE expected integer response");
}

Result<void> Client::flushall() {
    auto r = command({"FLUSHALL"});
    if (!r.ok) return Result<void>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString && r.value.str == "OK") {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::InvalidResponse, "FLUSHALL expected +OK");
}

Result<std::string> Client::stats() {
    auto r = command({"STATS"});
    if (!r.ok) return Result<std::string>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::string>::success(r.value.str);
    }
    return Result<std::string>::failure(ErrorCode::InvalidResponse, "STATS expected bulk string response");
}

Result<std::string> Client::monitor() {
    auto r = command({"MONITOR"});
    if (!r.ok) return Result<std::string>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::string>::success(r.value.str);
    }
    return Result<std::string>::failure(ErrorCode::InvalidResponse, "MONITOR expected bulk string response");
}

Result<void> Client::snapshot() {
    auto r = command({"SNAPSHOT"});
    if (!r.ok) return Result<void>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString && r.value.str == "OK") {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::InvalidResponse, "SNAPSHOT expected +OK");
}

Result<void> Client::health() {
    auto r = command({"HEALTH"});
    if (!r.ok) return Result<void>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString && r.value.str == "OK") {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::InvalidResponse, "HEALTH expected +OK");
}

Result<std::string> Client::debugInfo() {
    auto r = command({"DEBUG", "INFO"});
    if (!r.ok) return Result<std::string>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::string>::success(r.value.str);
    }
    return Result<std::string>::failure(ErrorCode::InvalidResponse, "DEBUG INFO expected bulk string response");
}

Result<void> Client::debugResetStats() {
    auto r = command({"DEBUG", "RESETSTATS"});
    if (!r.ok) return Result<void>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::SimpleString && r.value.str == "OK") {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::InvalidResponse, "DEBUG RESETSTATS expected +OK");
}

Result<int64_t> Client::expire(const std::string& key, int64_t seconds) {
    auto r = command({"EXPIRE", key, std::to_string(seconds)});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "EXPIRE expected integer response");
}

Result<int64_t> Client::ttl(const std::string& key) {
    auto r = command({"TTL", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "TTL expected integer response");
}

Result<int64_t> Client::pttl(const std::string& key) {
    auto r = command({"PTTL", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "PTTL expected integer response");
}

Result<int64_t> Client::persist(const std::string& key) {
    auto r = command({"PERSIST", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "PERSIST expected integer response");
}

Result<int64_t> Client::lpush(const std::string& key, const std::vector<std::string>& values) {
    if (values.empty()) return Result<int64_t>::failure(ErrorCode::InvalidArgument, "LPUSH requires values");
    std::vector<std::string> args{"LPUSH", key};
    args.insert(args.end(), values.begin(), values.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "LPUSH expected integer response");
}

Result<int64_t> Client::rpush(const std::string& key, const std::vector<std::string>& values) {
    if (values.empty()) return Result<int64_t>::failure(ErrorCode::InvalidArgument, "RPUSH requires values");
    std::vector<std::string> args{"RPUSH", key};
    args.insert(args.end(), values.begin(), values.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "RPUSH expected integer response");
}

Result<std::optional<std::string>> Client::lpop(const std::string& key) {
    auto r = command({"LPOP", key});
    if (!r.ok) return Result<std::optional<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::NullBulkString) return Result<std::optional<std::string>>::success(std::nullopt);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::optional<std::string>>::success(r.value.str);
    }
    return Result<std::optional<std::string>>::failure(ErrorCode::InvalidResponse, "LPOP expected bulk/null response");
}

Result<std::optional<std::string>> Client::rpop(const std::string& key) {
    auto r = command({"RPOP", key});
    if (!r.ok) return Result<std::optional<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::NullBulkString) return Result<std::optional<std::string>>::success(std::nullopt);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::optional<std::string>>::success(r.value.str);
    }
    return Result<std::optional<std::string>>::failure(ErrorCode::InvalidResponse, "RPOP expected bulk/null response");
}

Result<int64_t> Client::llen(const std::string& key) {
    auto r = command({"LLEN", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "LLEN expected integer response");
}

Result<std::optional<std::string>> Client::lindex(const std::string& key, int64_t index) {
    auto r = command({"LINDEX", key, std::to_string(index)});
    if (!r.ok) return Result<std::optional<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::NullBulkString) return Result<std::optional<std::string>>::success(std::nullopt);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::optional<std::string>>::success(r.value.str);
    }
    return Result<std::optional<std::string>>::failure(ErrorCode::InvalidResponse, "LINDEX expected bulk/null response");
}

Result<int64_t> Client::sadd(const std::string& key, const std::vector<std::string>& members) {
    if (members.empty()) return Result<int64_t>::failure(ErrorCode::InvalidArgument, "SADD requires members");
    std::vector<std::string> args{"SADD", key};
    args.insert(args.end(), members.begin(), members.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "SADD expected integer response");
}

Result<int64_t> Client::srem(const std::string& key, const std::vector<std::string>& members) {
    if (members.empty()) return Result<int64_t>::failure(ErrorCode::InvalidArgument, "SREM requires members");
    std::vector<std::string> args{"SREM", key};
    args.insert(args.end(), members.begin(), members.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "SREM expected integer response");
}

Result<int64_t> Client::scard(const std::string& key) {
    auto r = command({"SCARD", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "SCARD expected integer response");
}

Result<int64_t> Client::sismember(const std::string& key, const std::string& member) {
    auto r = command({"SISMEMBER", key, member});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "SISMEMBER expected integer response");
}

Result<std::vector<std::string>> Client::smembers(const std::string& key) {
    auto r = command({"SMEMBERS", key});
    if (!r.ok) return Result<std::vector<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type != RespType::Array) {
        return Result<std::vector<std::string>>::failure(ErrorCode::InvalidResponse, "SMEMBERS expected array response");
    }
    std::vector<std::string> out;
    out.reserve(r.value.array.size());
    for (const auto& v : r.value.array) {
        if (v.type != RespType::BulkString && v.type != RespType::SimpleString) {
            return Result<std::vector<std::string>>::failure(ErrorCode::InvalidResponse, "SMEMBERS array element must be string");
        }
        out.push_back(v.str);
    }
    return Result<std::vector<std::string>>::success(std::move(out));
}

Result<int64_t> Client::hset(const std::string& key,
                             const std::vector<std::pair<std::string, std::string>>& field_values) {
    if (field_values.empty()) {
        return Result<int64_t>::failure(ErrorCode::InvalidArgument, "HSET requires field-value pairs");
    }
    std::vector<std::string> args{"HSET", key};
    for (const auto& kv : field_values) {
        args.push_back(kv.first);
        args.push_back(kv.second);
    }
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "HSET expected integer response");
}

Result<std::optional<std::string>> Client::hget(const std::string& key, const std::string& field) {
    auto r = command({"HGET", key, field});
    if (!r.ok) return Result<std::optional<std::string>>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::NullBulkString) return Result<std::optional<std::string>>::success(std::nullopt);
    if (r.value.type == RespType::BulkString || r.value.type == RespType::SimpleString) {
        return Result<std::optional<std::string>>::success(r.value.str);
    }
    return Result<std::optional<std::string>>::failure(ErrorCode::InvalidResponse, "HGET expected bulk/null response");
}

Result<int64_t> Client::hdel(const std::string& key, const std::vector<std::string>& fields) {
    if (fields.empty()) return Result<int64_t>::failure(ErrorCode::InvalidArgument, "HDEL requires fields");
    std::vector<std::string> args{"HDEL", key};
    args.insert(args.end(), fields.begin(), fields.end());
    auto r = command(args);
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "HDEL expected integer response");
}

Result<int64_t> Client::hlen(const std::string& key) {
    auto r = command({"HLEN", key});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "HLEN expected integer response");
}

Result<int64_t> Client::hexists(const std::string& key, const std::string& field) {
    auto r = command({"HEXISTS", key, field});
    if (!r.ok) return Result<int64_t>::failure(r.error.code, r.error.message);
    if (r.value.type == RespType::Integer) return Result<int64_t>::success(r.value.integer);
    return Result<int64_t>::failure(ErrorCode::InvalidResponse, "HEXISTS expected integer response");
}

Result<std::vector<std::pair<std::string, std::string>>> Client::hgetall(const std::string& key) {
    auto r = command({"HGETALL", key});
    if (!r.ok) return Result<std::vector<std::pair<std::string, std::string>>>::failure(r.error.code, r.error.message);
    if (r.value.type != RespType::Array) {
        return Result<std::vector<std::pair<std::string, std::string>>>::failure(
            ErrorCode::InvalidResponse, "HGETALL expected array response");
    }
    if ((r.value.array.size() % 2) != 0) {
        return Result<std::vector<std::pair<std::string, std::string>>>::failure(
            ErrorCode::InvalidResponse, "HGETALL array size should be even");
    }
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(r.value.array.size() / 2);
    for (size_t i = 0; i < r.value.array.size(); i += 2) {
        const auto& k = r.value.array[i];
        const auto& v = r.value.array[i + 1];
        if ((k.type != RespType::BulkString && k.type != RespType::SimpleString) ||
            (v.type != RespType::BulkString && v.type != RespType::SimpleString)) {
            return Result<std::vector<std::pair<std::string, std::string>>>::failure(
                ErrorCode::InvalidResponse, "HGETALL array element must be string");
        }
        out.emplace_back(k.str, v.str);
    }
    return Result<std::vector<std::pair<std::string, std::string>>>::success(std::move(out));
}

Result<std::vector<std::optional<std::string>>> Client::mget(const std::vector<std::string>& keys) {
    if (keys.empty()) {
        return Result<std::vector<std::optional<std::string>>>::failure(ErrorCode::InvalidArgument, "empty keys");
    }
    std::vector<std::vector<std::string>> cmds;
    cmds.reserve(keys.size());
    for (const auto& k : keys) {
        cmds.push_back({"GET", k});
    }
    auto rp = pipeline(cmds);
    if (!rp.ok) {
        return Result<std::vector<std::optional<std::string>>>::failure(rp.error.code, rp.error.message);
    }
    std::vector<std::optional<std::string>> out;
    out.reserve(rp.value.size());
    for (const auto& v : rp.value) {
        if (v.type == RespType::NullBulkString) {
            out.push_back(std::nullopt);
        } else if (v.type == RespType::BulkString || v.type == RespType::SimpleString) {
            out.push_back(v.str);
        } else if (v.type == RespType::Error) {
            return Result<std::vector<std::optional<std::string>>>::failure(ErrorCode::ServerError, v.str);
        } else {
            return Result<std::vector<std::optional<std::string>>>::failure(
                ErrorCode::InvalidResponse, "MGET expected bulk/null response");
        }
    }
    return Result<std::vector<std::optional<std::string>>>::success(std::move(out));
}

Result<void> Client::mset(const std::vector<std::pair<std::string, std::string>>& kvs) {
    if (kvs.empty()) {
        return Result<void>::failure(ErrorCode::InvalidArgument, "empty kvs");
    }
    std::vector<std::vector<std::string>> cmds;
    cmds.reserve(kvs.size());
    for (const auto& kv : kvs) {
        cmds.push_back({"SET", kv.first, kv.second});
    }
    PipelineOptions opts;
    opts.fail_on_server_error = true;
    opts.close_on_server_error = true;
    auto rp = pipeline(cmds, opts);
    if (!rp.ok) {
        return Result<void>::failure(rp.error.code, rp.error.message);
    }
    for (const auto& v : rp.value) {
        if (!(v.type == RespType::SimpleString && v.str == "OK")) {
            return Result<void>::failure(ErrorCode::InvalidResponse, "MSET expected +OK responses");
        }
    }
    return Result<void>::success();
}

} // namespace sunkv::client
