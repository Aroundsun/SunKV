#include "server_test_helper.h"

#include <cassert>

using namespace server_test;

namespace {

void expectEq(const std::string& actual, const std::string& expected) {
    if (actual != expected) {
        assert(false && "response mismatch");
    }
}

void expectStartsWith(const std::string& actual, const std::string& prefix) {
    if (actual.rfind(prefix, 0) != 0) {
        assert(false && "response prefix mismatch");
    }
}

} // namespace

int main() {
    ServerFixture fixture{"pubsub_integration"};
    fixture.start();

    const int sub_fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    const int pub_fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(3));
    assert(sub_fd >= 0 && pub_fd >= 0);

    RespStreamReader sub_reader{sub_fd};
    RespStreamReader pub_reader{pub_fd};

    // Subscribe first channel.
    assert(sendAll(sub_fd, respArrayBulk({"SUBSCRIBE", "ch1"})));
    expectEq(sub_reader.recvOneRESP(std::chrono::seconds(2)),
             "*3\r\n$9\r\nsubscribe\r\n$3\r\nch1\r\n:1\r\n");

    // Publish and verify fan-out and publish return count.
    assert(sendAll(pub_fd, respArrayBulk({"PUBLISH", "ch1", "hello"})));
    expectEq(pub_reader.recvOneRESP(std::chrono::seconds(2)), ":1\r\n");
    expectEq(sub_reader.recvOneRESP(std::chrono::seconds(2)),
             "*3\r\n$7\r\nmessage\r\n$3\r\nch1\r\n$5\r\nhello\r\n");

    // Subscribe second channel and verify count increments.
    assert(sendAll(sub_fd, respArrayBulk({"SUBSCRIBE", "ch2"})));
    expectEq(sub_reader.recvOneRESP(std::chrono::seconds(2)),
             "*3\r\n$9\r\nsubscribe\r\n$3\r\nch2\r\n:2\r\n");

    // Subscribe mode gate: normal write should be rejected.
    assert(sendAll(sub_fd, respArrayBulk({"SET", "k", "v"})));
    expectStartsWith(sub_reader.recvOneRESP(std::chrono::seconds(2)), "-ERR only SUBSCRIBE/UNSUBSCRIBE/PING/QUIT");

    // Unsubscribe one channel then publish should return 0 receivers.
    assert(sendAll(sub_fd, respArrayBulk({"UNSUBSCRIBE", "ch1"})));
    expectEq(sub_reader.recvOneRESP(std::chrono::seconds(2)),
             "*3\r\n$11\r\nunsubscribe\r\n$3\r\nch1\r\n:1\r\n");
    assert(sendAll(pub_fd, respArrayBulk({"PUBLISH", "ch1", "after-unsub"})));
    expectEq(pub_reader.recvOneRESP(std::chrono::seconds(2)), ":0\r\n");

    // Disconnect subscriber and verify cleanup.
    ::close(sub_fd);
    const bool cleaned = waitUntil([&]() {
        if (!sendAll(pub_fd, respArrayBulk({"PUBLISH", "ch2", "after-close"}))) {
            return false;
        }
        return pub_reader.recvOneRESP(std::chrono::seconds(1)) == ":0\r\n";
    }, std::chrono::seconds(3), std::chrono::milliseconds(100));
    if (!cleaned) {
        assert(false && "subscriber cleanup was not observed");
    }

    ::close(pub_fd);
    fixture.stop();
    return 0;
}

