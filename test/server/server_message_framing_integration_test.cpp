#include "test/server/server_test_helper.h"

#include <string>
#include <vector>

using namespace server_test;

int main() {
    ServerFixture fixture("server_message_framing_integration_test");
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    assert(fd >= 0);

    RespStreamReader reader(fd);

    // 1) 单条请求
    const std::string reqPing = respArrayBulk(std::vector<std::string>{"PING"});
    assert(sendAll(fd, reqPing));
    const std::string respPing = reader.recvOneRESP(std::chrono::seconds(2));
    assert(respPing == "+PONG\r\n");

    // 2) pipeline：多条命令拼在同一次 write 中
    const std::string reqPipeline =
        respArrayBulk(std::vector<std::string>{"PING"}) +
        respArrayBulk(std::vector<std::string>{"SET", "key", "value"}) +
        respArrayBulk(std::vector<std::string>{"GET", "key"});

    assert(sendAll(fd, reqPipeline));

    const std::string resp1 = reader.recvOneRESP(std::chrono::seconds(2));
    const std::string resp2 = reader.recvOneRESP(std::chrono::seconds(2));
    const std::string resp3 = reader.recvOneRESP(std::chrono::seconds(2));

    assert(resp1 == "+PONG\r\n");
    assert(resp2 == "+OK\r\n");
    assert(resp3 == "$5\r\nvalue\r\n");

    // 3) 半包：SET 的最后 CRLF 缺失，应该等待下一次 read 拼齐
    const std::string fullSet = respArrayBulk(std::vector<std::string>{"SET", "key", "v2"});
    assert(fullSet.size() >= 2);

    const std::string part1 = fullSet.substr(0, fullSet.size() - 2); // drop last "\r\n"
    const std::string part2 = fullSet.substr(fullSet.size() - 2);

    assert(sendAll(fd, part1));

    // 在短窗口内不应收到响应
    const std::string early = reader.recvOneRESP(std::chrono::milliseconds(200));
    assert(early.empty());

    assert(sendAll(fd, part2));
    const std::string respSet = reader.recvOneRESP(std::chrono::seconds(2));
    assert(respSet == "+OK\r\n");

    ::close(fd);
    fixture.stop();
    return 0;
}

