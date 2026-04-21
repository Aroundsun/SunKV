#include "server_test_helper.h"

#include <cassert>

using namespace server_test;

namespace {

static void expectEq(const std::string& got, const std::string& expected) {
    if (got != expected) {
        assert(false && "response mismatch");
    }
}

static void expectStartsWith(const std::string& got, const std::string& prefix) {
    if (got.rfind(prefix, 0) != 0) {
        assert(false && "response prefix mismatch");
    }
}

} // namespace

int main() {
    ServerFixture fx{"transaction_multi_exec"};
    fx.start();

    const int fd = connectToHost(fx.host, fx.port, std::chrono::seconds(3));
    assert(fd >= 0);

    RespStreamReader rr{fd};

    // MULTI
    assert(sendAll(fd, respArrayBulk({"MULTI"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "+OK\r\n");

    // SET a 1 -> QUEUED
    assert(sendAll(fd, respArrayBulk({"SET", "a", "1"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "+QUEUED\r\n");

    // GET a -> QUEUED
    assert(sendAll(fd, respArrayBulk({"GET", "a"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "+QUEUED\r\n");

    // EXEC -> array of two results: +OK and bulk "1"
    assert(sendAll(fd, respArrayBulk({"EXEC"})));
    const std::string exec_resp = rr.recvOneRESP(std::chrono::seconds(2));
    expectEq(exec_resp, "*2\r\n+OK\r\n$1\r\n1\r\n");

    // EXEC without MULTI
    assert(sendAll(fd, respArrayBulk({"EXEC"})));
    expectStartsWith(rr.recvOneRESP(std::chrono::seconds(2)), "-ERR ");

    // DISCARD without MULTI
    assert(sendAll(fd, respArrayBulk({"DISCARD"})));
    expectStartsWith(rr.recvOneRESP(std::chrono::seconds(2)), "-ERR ");

    // Nested MULTI
    assert(sendAll(fd, respArrayBulk({"MULTI"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "+OK\r\n");
    assert(sendAll(fd, respArrayBulk({"MULTI"})));
    expectStartsWith(rr.recvOneRESP(std::chrono::seconds(2)), "-ERR ");
    assert(sendAll(fd, respArrayBulk({"DISCARD"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "+OK\r\n");

    // WATCH / UNWATCH explicitly unsupported
    assert(sendAll(fd, respArrayBulk({"WATCH", "a"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "-ERR WATCH/UNWATCH is not supported\r\n");
    assert(sendAll(fd, respArrayBulk({"UNWATCH"})));
    expectEq(rr.recvOneRESP(std::chrono::seconds(2)), "-ERR WATCH/UNWATCH is not supported\r\n");

    ::close(fd);
    fx.stop();
    return 0;
}

