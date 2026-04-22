#include "test/server/server_test_helper.h"

#include <vector>

using namespace server_test;

namespace {

bool runSetGetLoad(const std::string& host, int port, int concurrency, int pipelineDepth) {
    for (int workerIndex = 0; workerIndex < concurrency; ++workerIndex) {
        const int socketFd = connectToHost(host, port, std::chrono::seconds(3));
        if (socketFd < 0) {
            return false;
        }
        RespStreamReader reader(socketFd);
        std::vector<std::string> keys;
        keys.reserve(static_cast<size_t>(pipelineDepth));

        for (int i = 0; i < pipelineDepth; ++i) {
            const std::string key = "m:set:" + std::to_string(workerIndex) + ":" + std::to_string(i);
            const std::string value = "v" + std::to_string(i);
            keys.push_back(key);
            if (!sendAll(socketFd, respArrayBulk({"SET", key, value}))) {
                ::close(socketFd);
                return false;
            }
        }
        for (int i = 0; i < pipelineDepth; ++i) {
            if (reader.recvOneRESP(std::chrono::seconds(2)) != "+OK\r\n") {
                ::close(socketFd);
                return false;
            }
        }

        for (int i = 0; i < pipelineDepth; ++i) {
            if (!sendAll(socketFd, respArrayBulk({"GET", keys[static_cast<size_t>(i)]}))) {
                ::close(socketFd);
                return false;
            }
        }
        for (int i = 0; i < pipelineDepth; ++i) {
            const std::string response = reader.recvOneRESP(std::chrono::seconds(2));
            if (response.rfind('$', 0) != 0) {
                ::close(socketFd);
                return false;
            }
        }
        ::close(socketFd);
    }
    return true;
}

bool runTransactionSanity(const std::string& host, int port) {
    const int socketFd = connectToHost(host, port, std::chrono::seconds(3));
    if (socketFd < 0) {
        return false;
    }
    RespStreamReader reader(socketFd);

    if (!sendAll(socketFd, respArrayBulk({"MULTI"})) || reader.recvOneRESP(std::chrono::seconds(2)) != "+OK\r\n") {
        ::close(socketFd);
        return false;
    }
    if (!sendAll(socketFd, respArrayBulk({"SET", "matrix:txn:key", "txn_v"})) ||
        reader.recvOneRESP(std::chrono::seconds(2)) != "+QUEUED\r\n") {
        ::close(socketFd);
        return false;
    }
    if (!sendAll(socketFd, respArrayBulk({"GET", "matrix:txn:key"})) ||
        reader.recvOneRESP(std::chrono::seconds(2)) != "+QUEUED\r\n") {
        ::close(socketFd);
        return false;
    }
    if (!sendAll(socketFd, respArrayBulk({"EXEC"}))) {
        ::close(socketFd);
        return false;
    }
    const std::string execResp = reader.recvOneRESP(std::chrono::seconds(2));
    if (execResp != "*2\r\n+OK\r\n$5\r\ntxn_v\r\n") {
        ::close(socketFd);
        return false;
    }
    ::close(socketFd);
    return true;
}

bool runPubSubSanity(const std::string& host, int port) {
    const int subFd = connectToHost(host, port, std::chrono::seconds(3));
    const int pubFd = connectToHost(host, port, std::chrono::seconds(3));
    if (subFd < 0 || pubFd < 0) {
        if (subFd >= 0) {
            ::close(subFd);
        }
        if (pubFd >= 0) {
            ::close(pubFd);
        }
        return false;
    }
    RespStreamReader subReader(subFd);
    RespStreamReader pubReader(pubFd);

    if (!sendAll(subFd, respArrayBulk({"SUBSCRIBE", "matrix:ch"})) ||
        subReader.recvOneRESP(std::chrono::seconds(2)) != "*3\r\n$9\r\nsubscribe\r\n$9\r\nmatrix:ch\r\n:1\r\n") {
        ::close(subFd);
        ::close(pubFd);
        return false;
    }

    if (!sendAll(pubFd, respArrayBulk({"PUBLISH", "matrix:ch", "hello"})) ||
        pubReader.recvOneRESP(std::chrono::seconds(2)) != ":1\r\n" ||
        subReader.recvOneRESP(std::chrono::seconds(2)) != "*3\r\n$7\r\nmessage\r\n$9\r\nmatrix:ch\r\n$5\r\nhello\r\n") {
        ::close(subFd);
        ::close(pubFd);
        return false;
    }

    if (!sendAll(subFd, respArrayBulk({"UNSUBSCRIBE", "matrix:ch"})) ||
        subReader.recvOneRESP(std::chrono::seconds(2)) != "*3\r\n$11\r\nunsubscribe\r\n$9\r\nmatrix:ch\r\n:0\r\n") {
        ::close(subFd);
        ::close(pubFd);
        return false;
    }

    ::close(subFd);
    ::close(pubFd);
    return true;
}

} // namespace

int main() {
    constexpr int kSetGetFailure = 10;
    constexpr int kTxnFailure = 11;
    constexpr int kPubSubFailure = 12;
    ServerFixture fixture("server_regression_matrix_test");
    fixture.start();

    const std::vector<int> concurrencies = {1, 10, 50};
    const std::vector<int> pipelineDepths = {1, 16};
    for (const int concurrency : concurrencies) {
        for (const int pipelineDepth : pipelineDepths) {
            if (!runSetGetLoad(fixture.host, fixture.port, concurrency, pipelineDepth)) {
                fixture.stop();
                return kSetGetFailure;
            }
        }
    }

    if (!runTransactionSanity(fixture.host, fixture.port)) {
        fixture.stop();
        return kTxnFailure;
    }
    if (!runPubSubSanity(fixture.host, fixture.port)) {
        fixture.stop();
        return kPubSubFailure;
    }

    fixture.stop();
    return 0;
}
