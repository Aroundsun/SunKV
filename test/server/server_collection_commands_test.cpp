#include "test/server/server_test_helper.h"

#include <string>
#include <vector>

using namespace server_test;

static bool must(bool cond) {
    return cond;
}

static std::string recvOne(RespStreamReader& reader) {
    const std::string resp = reader.recvOneRESP(std::chrono::seconds(2));
    if (!must(!resp.empty())) {
        return std::string();
    }
    return resp;
}

int main() {
    ServerFixture fixture("server_collection_commands_test");
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    if (!must(fd >= 0)) return 10;

    RespStreamReader reader(fd);

    // List: LPUSH/LLEN/LINDEX/LPOP
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"LPUSH", "list", "1", "2"});
        if (!must(sendAll(fd, req))) return 11;
        if (!must(recvOne(reader) == ":2\r\n")) return 12;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LLEN", "list"})))) return 13;
        if (!must(recvOne(reader) == ":2\r\n")) return 14;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LINDEX", "list", "0"})))) return 15;
        if (!must(recvOne(reader) == "$1\r\n2\r\n")) return 16;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LPOP", "list"})))) return 17;
        if (!must(recvOne(reader) == "$1\r\n2\r\n")) return 18;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LPOP", "list"})))) return 19;
        if (!must(recvOne(reader) == "$1\r\n1\r\n")) return 20;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LPOP", "list"})))) return 21;
        if (!must(recvOne(reader) == "$-1\r\n")) return 22;
    }

    // WRONGTYPE：字符串 vs list
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SET", "str", "v"})))) return 23;
        if (!must(recvOne(reader) == "+OK\r\n")) return 24;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"LPUSH", "str", "a"})))) return 25;
        if (!must(recvOne(reader) ==
               "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n")) return 26;
    }

    // Set: SADD/SCARD/SISMEMBER/SREM
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SADD", "set", "a", "b", "c"})))) return 27;
        if (!must(recvOne(reader) == ":3\r\n")) return 28;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SCARD", "set"})))) return 29;
        if (!must(recvOne(reader) == ":3\r\n")) return 30;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SISMEMBER", "set", "b"})))) return 31;
        if (!must(recvOne(reader) == ":1\r\n")) return 32;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SISMEMBER", "set", "z"})))) return 33;
        if (!must(recvOne(reader) == ":0\r\n")) return 34;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SREM", "set", "b", "c"})))) return 35;
        if (!must(recvOne(reader) == ":2\r\n")) return 36;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SCARD", "set"})))) return 37;
        if (!must(recvOne(reader) == ":1\r\n")) return 38;
    }

    // Hash: HSET/HGET/HEXISTS/HGET missing
    {
        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"HSET", "h", "f1", "v1", "f2", "v2"})))) return 39;
        if (!must(recvOne(reader) == ":2\r\n")) return 40;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"HGET", "h", "f1"})))) return 41;
        if (!must(recvOne(reader) == "$2\r\nv1\r\n")) return 42;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"HEXISTS", "h", "f2"})))) return 43;
        if (!must(recvOne(reader) == ":1\r\n")) return 44;

        if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"HGET", "h", "missing"})))) return 45;
        if (!must(recvOne(reader) == "$-1\r\n")) return 46;
    }

    ::close(fd);
    fixture.stop();
    return 0;
}

