#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "client/include/RespValue.h"
#include "client/include/Result.h"

namespace sunkv::client {

class Client {
public:
    struct Options {
        std::string host{"127.0.0.1"};
        uint16_t port{6380};
        int connect_timeout_ms{1000};
        int read_timeout_ms{2000};
        int write_timeout_ms{2000};
    };

    explicit Client(Options options);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Result<void> connect();
    void close();
    bool isConnected() const;

    Result<RespValue> command(const std::vector<std::string>& args);
    struct PipelineOptions {
        bool fail_on_server_error{false};
        bool close_on_server_error{true};
    };
    Result<std::vector<RespValue>> pipeline(const std::vector<std::vector<std::string>>& commands);
    Result<std::vector<RespValue>> pipeline(const std::vector<std::vector<std::string>>& commands,
                                            const PipelineOptions& options);

    Result<std::string> ping();
    Result<std::optional<std::string>> get(const std::string& key);
    Result<void> set(const std::string& key, const std::string& value);
    Result<int64_t> del(const std::string& key);
    Result<int64_t> exists(const std::vector<std::string>& keys);
    Result<std::vector<std::string>> keys();
    Result<int64_t> dbsize();
    Result<void> flushall();
    Result<std::string> stats();
    Result<std::string> monitor();
    Result<void> snapshot();
    Result<void> health();
    Result<std::string> debugInfo();
    Result<void> debugResetStats();
    Result<int64_t> expire(const std::string& key, int64_t seconds);
    Result<int64_t> ttl(const std::string& key);
    Result<int64_t> pttl(const std::string& key);
    Result<int64_t> persist(const std::string& key);

    Result<int64_t> lpush(const std::string& key, const std::vector<std::string>& values);
    Result<int64_t> rpush(const std::string& key, const std::vector<std::string>& values);
    Result<std::optional<std::string>> lpop(const std::string& key);
    Result<std::optional<std::string>> rpop(const std::string& key);
    Result<int64_t> llen(const std::string& key);
    Result<std::optional<std::string>> lindex(const std::string& key, int64_t index);

    Result<int64_t> sadd(const std::string& key, const std::vector<std::string>& members);
    Result<int64_t> srem(const std::string& key, const std::vector<std::string>& members);
    Result<int64_t> scard(const std::string& key);
    Result<int64_t> sismember(const std::string& key, const std::string& member);
    Result<std::vector<std::string>> smembers(const std::string& key);

    Result<int64_t> hset(const std::string& key, const std::vector<std::pair<std::string, std::string>>& field_values);
    Result<std::optional<std::string>> hget(const std::string& key, const std::string& field);
    Result<int64_t> hdel(const std::string& key, const std::vector<std::string>& fields);
    Result<int64_t> hlen(const std::string& key);
    Result<int64_t> hexists(const std::string& key, const std::string& field);
    Result<std::vector<std::pair<std::string, std::string>>> hgetall(const std::string& key);
    Result<std::vector<std::optional<std::string>>> mget(const std::vector<std::string>& keys);
    Result<void> mset(const std::vector<std::pair<std::string, std::string>>& kvs);
    Result<void> multi();
    Result<std::vector<RespValue>> exec();
    Result<void> discard();
    Result<int64_t> publish(const std::string& channel, const std::string& payload);
    Result<RespValue> subscribe(const std::vector<std::string>& channels);
    Result<RespValue> unsubscribe(const std::vector<std::string>& channels = {});

private:
    struct Impl;
    Impl* impl_;
};

} // namespace sunkv::client
