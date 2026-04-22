#include "test/server/server_test_helper.h"

#include <cassert>

using namespace server_test;

int main() {
    ServerFixture fixture("server_resp_error_recovery_test");
    fixture.start();

    // 连接 1：发送非法 RESP，验证不会把服务打挂。
    const int badFd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    if (badFd < 0) {
        fixture.stop();
        return 10;
    }
    RespStreamReader badReader(badFd);
    if (!sendAll(badFd, "xInvalid\r\n")) {
        ::close(badFd);
        fixture.stop();
        return 11;
    }
    (void)badReader.recvOneRESP(std::chrono::seconds(2));
    ::close(badFd);

    // 连接 2：服务仍能处理合法命令，说明异常输入后的恢复可用。
    const int goodFd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    if (goodFd < 0) {
        fixture.stop();
        return 12;
    }
    RespStreamReader goodReader(goodFd);
    if (!sendAll(goodFd, respArrayBulk({"PING"}))) {
        ::close(goodFd);
        fixture.stop();
        return 13;
    }
    if (goodReader.recvOneRESP(std::chrono::seconds(2)) != "+PONG\r\n") {
        ::close(goodFd);
        fixture.stop();
        return 14;
    }
    ::close(goodFd);

    fixture.stop();
    return 0;
}
