#include "test/server/server_test_helper.h"

#include <chrono>
#include <string>

using namespace server_test;

int main() {
    ServerFixture fixture("server_connection_lifecycle_test");
    fixture.start();

    auto stats0 = fixture.getStats();
    assert(stats0.current_connections == 0);

    // 1) 连上后 current_connections 应变为 1
    const int fd1 = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    assert(fd1 >= 0);

    const bool incOk = waitUntil([&]() {
        auto s = fixture.getStats();
        return s.current_connections == 1;
    }, std::chrono::seconds(2));
    assert(incOk);

    // 2) 断开后 current_connections 回到 0
    ::close(fd1);

    const bool decOk = waitUntil([&]() {
        auto s = fixture.getStats();
        return s.current_connections == 0;
    }, std::chrono::seconds(2));
    assert(decOk);

    // 3) 断开时带有残余半包，不应影响下一次新连接的正确响应
    {
        const int fdPartial = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
        assert(fdPartial >= 0);

        // 只发到 "PIN"（缺少完整 PING\r\n），期望没有响应且断开后不会污染下一连接
        const std::string reqPartial = "$4\r\nPIN";
        assert(sendAll(fdPartial, reqPartial));
        ::close(fdPartial);
    }

    waitUntil([&]() {
        return fixture.getStats().current_connections == 0;
    }, std::chrono::seconds(2));

    // 新连接应可正常使用
    const int fd2 = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    assert(fd2 >= 0);

    RespStreamReader reader(fd2);
    const std::string reqPing = respArrayBulk(std::vector<std::string>{"PING"});
    assert(sendAll(fd2, reqPing));
    const std::string resp = reader.recvOneRESP(std::chrono::seconds(2));
    assert(resp == "+PONG\r\n");
    ::close(fd2);

    waitUntil([&]() {
        return fixture.getStats().current_connections == 0;
    }, std::chrono::seconds(2));

    fixture.stop();
    return 0;
}

