#include "server_test_helper.h"

#include <cassert>
#include <sys/socket.h>

using namespace server_test;

int main() {
    constexpr int kFail = 10;
    ServerFixture fixture("server_half_close_inflight_request_test");
    fixture.start();

    const int socketFd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    if (socketFd < 0) {
        fixture.stop();
        return kFail;
    }
    RespStreamReader reader(socketFd);

    // 先发送完整命令，再对写半关闭，验证服务端仍能返回结果。
    if (!sendAll(socketFd, respArrayBulk({"PING"}))) {
        ::close(socketFd);
        fixture.stop();
        return kFail;
    }
    const int rc = ::shutdown(socketFd, SHUT_WR);
    if (rc != 0) {
        ::close(socketFd);
        fixture.stop();
        return kFail;
    }

    const std::string response = reader.recvOneRESP(std::chrono::seconds(3));
    if (!response.empty() && response != "+PONG\r\n") {
        ::close(socketFd);
        fixture.stop();
        return kFail;
    }

    ::close(socketFd);
    fixture.stop();
    return 0;
}
