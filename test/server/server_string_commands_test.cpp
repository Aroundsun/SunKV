#include "test/server/server_test_helper.h"

#include <memory>
#include <string>
#include <vector>

using namespace server_test;

static bool requireTrue(bool cond) {
    return cond;
}

static std::string recvAndAssertEquals(RespStreamReader& reader,
                                       const std::string& expected,
                                       std::chrono::milliseconds timeout) {
    const std::string got = reader.recvOneRESP(timeout);
    if (!requireTrue(got == expected)) {
        return std::string();
    }
    return got;
}

int main() {
    ServerFixture fixture("server_string_commands_test");
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    if (!requireTrue(fd >= 0)) return 10;

    RespStreamReader reader(fd);

    // PING
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"PING"});
        if (!requireTrue(sendAll(fd, req))) return 11;
        if (recvAndAssertEquals(reader, "+PONG\r\n", std::chrono::seconds(2)).empty()) return 12;
    }

    // SET / GET
    {
        const std::string reqSet = respArrayBulk(std::vector<std::string>{"SET", "key", "value"});
        if (!requireTrue(sendAll(fd, reqSet))) return 13;
        if (recvAndAssertEquals(reader, "+OK\r\n", std::chrono::seconds(2)).empty()) return 14;

        const std::string reqGet = respArrayBulk(std::vector<std::string>{"GET", "key"});
        if (!requireTrue(sendAll(fd, reqGet))) return 15;
        if (recvAndAssertEquals(reader, "$5\r\nvalue\r\n", std::chrono::seconds(2)).empty()) return 16;

        const std::string reqGetMissing = respArrayBulk(std::vector<std::string>{"GET", "missing"});
        if (!requireTrue(sendAll(fd, reqGetMissing))) return 17;
        if (recvAndAssertEquals(reader, "$-1\r\n", std::chrono::seconds(2)).empty()) return 18;
    }

    // Case-insensitive command name: get
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"get", "key"});
        if (!requireTrue(sendAll(fd, req))) return 19;
        if (recvAndAssertEquals(reader, "$5\r\nvalue\r\n", std::chrono::seconds(2)).empty()) return 20;
    }

    // EXISTS
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"EXISTS", "key", "missing"});
        if (!requireTrue(sendAll(fd, req))) return 21;
        if (recvAndAssertEquals(reader, ":1\r\n", std::chrono::seconds(2)).empty()) return 22;
    }

    // KEYS：解析 RESPArray 并断言包含指定 key
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"KEYS"});
        if (!requireTrue(sendAll(fd, req))) return 23;
        const std::string resp = reader.recvOneRESP(std::chrono::seconds(2));
        if (!requireTrue(!resp.empty())) return 24;

        RESPParser p;
        const ParseResult r = p.parse(resp);
        if (!requireTrue(r.success && r.complete && r.value)) return 25;
        if (!requireTrue(r.value->isArray())) return 26;
        auto arr = std::dynamic_pointer_cast<RESPArray>(r.value);
        if (!requireTrue(arr && arr->size() >= 1)) return 27;
        bool found = false;
        for (const auto& v : arr->getValues()) {
            if (v && v->toString() == "key") {
                found = true;
                break;
            }
        }
        if (!requireTrue(found)) return 28;
    }

    // DBSIZE
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"DBSIZE"});
        if (!requireTrue(sendAll(fd, req))) return 29;
        if (recvAndAssertEquals(reader, ":1\r\n", std::chrono::seconds(2)).empty()) return 30;
    }

    // DEL / DBSIZE
    {
        const std::string req = respArrayBulk(std::vector<std::string>{"DEL", "key"});
        if (!requireTrue(sendAll(fd, req))) return 31;
        if (recvAndAssertEquals(reader, ":1\r\n", std::chrono::seconds(2)).empty()) return 32;

        const std::string reqDb = respArrayBulk(std::vector<std::string>{"DBSIZE"});
        if (!requireTrue(sendAll(fd, reqDb))) return 33;
        if (recvAndAssertEquals(reader, ":0\r\n", std::chrono::seconds(2)).empty()) return 34;
    }

    // FLUSHALL
    {
        const std::string reqSet = respArrayBulk(std::vector<std::string>{"SET", "k2", "v2"});
        if (!requireTrue(sendAll(fd, reqSet))) return 35;
        if (recvAndAssertEquals(reader, "+OK\r\n", std::chrono::seconds(2)).empty()) return 36;

        const std::string req = respArrayBulk(std::vector<std::string>{"FLUSHALL"});
        if (!requireTrue(sendAll(fd, req))) return 37;
        if (recvAndAssertEquals(reader, "+OK\r\n", std::chrono::seconds(2)).empty()) return 38;

        const std::string reqDb = respArrayBulk(std::vector<std::string>{"DBSIZE"});
        if (!requireTrue(sendAll(fd, reqDb))) return 39;
        if (recvAndAssertEquals(reader, ":0\r\n", std::chrono::seconds(2)).empty()) return 40;
    }

    ::close(fd);
    fixture.stop();
    return 0;
}

