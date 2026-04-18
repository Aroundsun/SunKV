#include "test/server/server_test_helper.h"

#include <chrono>
#include <string>

using namespace server_test;

int main() {
    ServerFixture fixture("server_shutdown_integration_test");
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    assert(fd >= 0);

    RespStreamReader reader(fd);
    const std::string reqPing = respArrayBulk(std::vector<std::string>{"PING"});
    assert(sendAll(fd, reqPing));
    const std::string resp = reader.recvOneRESP(std::chrono::seconds(2));
    assert(resp == "+PONG\r\n");

    ::close(fd);

    // 等到 current_connections 回到 0，避免 stop 阶段等待过久
    (void)waitUntil([&]() {
        return fixture.getStats().current_connections == 0;
    }, std::chrono::seconds(2));

    const auto t0 = std::chrono::steady_clock::now();
    fixture.stop();
    fixture.stop(); // 幂等：不应崩溃
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (elapsedMs >= 5000) {
        assert(false && "server.stop() took too long");
    }

    // stop 后端口应不可连接
    const int fd2 = connectToHost(fixture.host, fixture.port, std::chrono::milliseconds(200));
    if (fd2 >= 0) {
        ::close(fd2);
        assert(false && "expected connect to fail after stop");
    }

    return 0;
}

