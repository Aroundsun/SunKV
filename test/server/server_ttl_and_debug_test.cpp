#include "test/server/server_test_helper.h"

#include <string>
#include <vector>

using namespace server_test;

static bool must(bool cond) {
    return cond;
}

static std::string recv(RespStreamReader& reader) {
    const std::string resp = reader.recvOneRESP(std::chrono::seconds(3));
    if (!must(!resp.empty())) {
        return std::string();
    }
    return resp;
}

static int64_t parseIntegerValue(const std::string& respRaw) {
    RESPParser p;
    auto r = p.parse(respRaw);
    if (!must(r.success && r.complete && r.value)) return 0;
    if (!must(r.value->isInteger())) return 0;
    const std::string s = r.value->toString();
    return std::stoll(s);
}

int main() {
    ServerFixture fixture("server_ttl_and_debug_test");
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    if (!must(fd >= 0)) return 10;

    RespStreamReader reader(fd);

    // HEALTH
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"HEALTH"});
        if (!must(sendAll(fd, req))) return 11;
        if (!must(recv(reader) == "+OK\r\n")) return 12;
    }

    // TTL missing key => -2
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"TTL", "missing"});
        if (!must(sendAll(fd, req))) return 13;
        if (!must(recv(reader) == ":-2\r\n")) return 14;
    }

    // SET + TTL no-ttl => -1
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SET", "k", "v"})))) return 15;
        if (!must(recv(reader) == "+OK\r\n")) return 16;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"TTL", "k"})))) return 17;
        if (!must(recv(reader) == ":-1\r\n")) return 18;
    }

    // EXPIRE invalid arg
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"EXPIRE", "k", "abc"});
        if (!must(sendAll(fd, req))) return 19;
        if (!must(recv(reader) == "-Invalid TTL value\r\n")) return 20;
    }

    // EXPIRE ok + TTL >= 0
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"EXPIRE", "k", "2"});
        if (!must(sendAll(fd, req))) return 21;
        if (!must(recv(reader) == ":1\r\n")) return 22;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"TTL", "k"})))) return 23;
        const std::string resp = recv(reader);
        const int64_t ttl = parseIntegerValue(resp);
        if (!must(ttl >= 0)) return 24;
    }

    // PTTL exists (>=0)
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"PTTL", "k"})))) return 25;
        const std::string resp = recv(reader);
        const int64_t pttl = parseIntegerValue(resp);
        if (!must(pttl >= 0)) return 26;
    }

    // PERSIST should remove ttl => TTL becomes -1
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"PERSIST", "k"})))) return 27;
        if (!must(recv(reader) == ":1\r\n")) return 28;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"TTL", "k"})))) return 29;
        if (!must(recv(reader) == ":-1\r\n")) return 30;
    }

    // STATS / MONITOR return bulk string containing key fields
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"STATS"})))) return 31;
        const std::string respStats = recv(reader);

        RESPParser p;
        auto r = p.parse(respStats);
        if (!must(r.success && r.complete && r.value)) return 32;
        if (!must(r.value->isBulkString())) return 33;
        if (!must(r.value->toString().find("uptime_seconds=") != std::string::npos)) return 34;
    }

    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"MONITOR"})))) return 35;
        const std::string resp = recv(reader);

        RESPParser p;
        auto r = p.parse(resp);
        if (!must(r.success && r.complete && r.value)) return 36;
        if (!must(r.value->isBulkString())) return 37;
        if (!must(r.value->toString().find("total_connections=") != std::string::npos)) return 38;
    }

    // DEBUG INFO => bulk string
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"DEBUG", "INFO"})))) return 39;
        const std::string resp = recv(reader);

        RESPParser p;
        auto r = p.parse(resp);
        if (!must(r.success && r.complete && r.value)) return 40;
        if (!must(r.value->isBulkString())) return 41;
        if (!must(r.value->toString().find("uptime_seconds=") != std::string::npos)) return 42;
    }

    // DEBUG RESETSTATS => +OK
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"DEBUG", "RESETSTATS"})))) return 43;
        if (!must(recv(reader) == "+OK\r\n")) return 44;
    }

    // DEBUG unknown subcommand
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"DEBUG", "NOPE"})))) return 45;
        if (!must(recv(reader) == "-ERR unknown DEBUG subcommand\r\n")) return 46;
    }

    // DEBUG missing subcommand
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"DEBUG"})))) return 47;
        if (!must(recv(reader) == "-ERR wrong number of arguments for 'DEBUG' command\r\n")) return 48;
    }

    ::close(fd);
    fixture.stop();
    return 0;
}

