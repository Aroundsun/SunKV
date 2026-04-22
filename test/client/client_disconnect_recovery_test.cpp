#include <cassert>

#include "../../client/include/Client.h"
#include "../server/server_test_helper.h"

using namespace sunkv::client;
using namespace server_test;

int main() {
    constexpr int kConnectTimeoutMs = 1000;
    constexpr int kReadWriteTimeoutMs = 1500;
    ServerFixture first("client_disconnect_recovery_first");
    first.start();

    Client::Options firstOptions;
    firstOptions.host = first.host;
    firstOptions.port = static_cast<uint16_t>(first.port);
    firstOptions.connect_timeout_ms = kConnectTimeoutMs;
    firstOptions.read_timeout_ms = kReadWriteTimeoutMs;
    firstOptions.write_timeout_ms = kReadWriteTimeoutMs;

    Client firstClient(firstOptions);
    auto connectResult = firstClient.connect();
    assert(connectResult.ok);
    auto pingResult = firstClient.ping();
    assert(pingResult.ok && pingResult.value == "PONG");

    // 主动断开客户端，再停止服务端，避免在关闭阶段保留在途连接导致析构竞态。
    firstClient.close();
    first.stop();
    auto getAfterStop = firstClient.get("k");
    assert(!getAfterStop.ok);

    // 使用新实例重新连接，验证恢复路径可用。
    ServerFixture second("client_disconnect_recovery_second");
    second.start();
    Client::Options secondOptions;
    secondOptions.host = second.host;
    secondOptions.port = static_cast<uint16_t>(second.port);
    secondOptions.connect_timeout_ms = kConnectTimeoutMs;
    secondOptions.read_timeout_ms = kReadWriteTimeoutMs;
    secondOptions.write_timeout_ms = kReadWriteTimeoutMs;

    Client secondClient(secondOptions);
    auto secondConnect = secondClient.connect();
    assert(secondConnect.ok);
    auto secondPing = secondClient.ping();
    assert(secondPing.ok && secondPing.value == "PONG");

    secondClient.close();
    second.stop();
    return 0;
}
