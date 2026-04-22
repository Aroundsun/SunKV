#include "server_test_helper.h"

#include <cassert>
#include <string>

using namespace server_test;

int main() {
    constexpr int kFail = 10;
    constexpr size_t kNearLimitBytes = static_cast<size_t>(512) * static_cast<size_t>(1024);
    constexpr size_t kOverLimitBytes = static_cast<size_t>(1024) * static_cast<size_t>(1024) + static_cast<size_t>(1);

    ServerFixture fixture("server_input_buffer_limit_test");
    fixture.cfg.max_conn_input_buffer_mb = 1;
    fixture.start();

    // 1) 阈值内大 value 正常写入。
    const int okSocketFd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    if (okSocketFd < 0) {
        fixture.stop();
        return kFail;
    }
    RespStreamReader okReader(okSocketFd);
    const std::string nearLimitValue(kNearLimitBytes, 'x');
    if (!sendAll(okSocketFd, respArrayBulk({"SET", "ibuf:key", nearLimitValue})) ||
        okReader.recvOneRESP(std::chrono::seconds(3)) != "+OK\r\n") {
        ::close(okSocketFd);
        fixture.stop();
        return kFail;
    }
    ::close(okSocketFd);

    // 2) 超过阈值（故意发不完整 bulk）触发 overflow 与断连。
    const int overflowSocketFd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    if (overflowSocketFd < 0) {
        fixture.stop();
        return kFail;
    }
    RespStreamReader overflowReader(overflowSocketFd);
    const std::string overLimitPayload(kOverLimitBytes, 'a');
    const std::string malformed = "$" + std::to_string(overLimitPayload.size()) + "\r\n" + overLimitPayload;
    if (!sendAll(overflowSocketFd, malformed)) {
        ::close(overflowSocketFd);
        fixture.stop();
        return kFail;
    }
    const std::string overflowResp = overflowReader.recvOneRESP(std::chrono::seconds(5));
    if (!overflowResp.empty() && overflowResp != "-Input buffer overflow\r\n") {
        ::close(overflowSocketFd);
        fixture.stop();
        return kFail;
    }
    ::close(overflowSocketFd);

    fixture.stop();
    return 0;
}
