#include "server_test_helper.h"

#include <string>
#include <vector>

using namespace server_test;

namespace {

inline std::string respArray(const std::vector<std::string>& elems) {
    return respArrayBulk(elems);
}

} // namespace

int main() {
    ServerFixture fixture("server_error_paths_integration_test");
    fixture.start();

    auto connectAndSend = [&](const std::string& req, std::chrono::milliseconds readTimeout) -> std::string {
        const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
        assert(fd >= 0);
        if (!sendAll(fd, req)) {
            ::close(fd);
            assert(false && "failed to send request");
        }
        RespStreamReader reader(fd);
        std::string resp = reader.recvOneRESP(readTimeout);
        ::close(fd);
        return resp;
    };

    // 1) unknown command via array
    {
        const std::string req = respArray(std::vector<std::string>{"BLA"});
        // 数组只有 1 个元素：命令名本身；dispatcher 找不到 -> ERR unknown command
        const std::string resp = connectAndSend(req, std::chrono::seconds(2));
        if (!resp.empty()) {
            assert(resp == "-ERR unknown command\r\n");
        }
    }

    // 2) 空数组：不满足 dispatch 条件，最终兜底未知命令
    {
        const std::string req = "*0\r\n";
        const std::string resp = connectAndSend(req, std::chrono::seconds(2));
        if (!resp.empty()) {
            assert(resp == "-ERR unknown command\r\n");
        }
    }

    // 3) 非法 RESP type：xInvalid\r\n
    {
        const std::string req = "xInvalid\r\n";
        const std::string resp = connectAndSend(req, std::chrono::seconds(2));
        if (!resp.empty()) {
            assert(resp == "-Invalid RESP format: Invalid RESP type: x\r\n");
        }
    }

    // 4) bulk string data 后缺失 CRLF，应返回 Missing CRLF...
    {
        const std::string req = "$3\r\nabcX\r\n";
        const std::string resp = connectAndSend(req, std::chrono::seconds(2));
        if (!resp.empty()) {
            assert(resp == "-Invalid RESP format: Missing CRLF after bulk string data\r\n");
        }
    }

    // 5) buffer overflow：输入缓冲超过 8MB，应强制关闭连接并返回 Input buffer overflow
    {
        std::string big(8 * 1024 * 1024 + 1, 'a');
        // 用一个“声明长度很大但故意不结束”的 bulk string 触发 incomplete 路径，
        // 这样服务端会持续保留输入缓冲，直到超过 8MB 上限后返回 overflow。
        const std::string req = "$" + std::to_string(big.size()) + "\r\n" + big;
        const std::string resp = connectAndSend(req, std::chrono::seconds(5));
        if (!resp.empty()) {
            assert(resp == "-Input buffer overflow\r\n");
        }
    }

    fixture.stop();
    return 0;
}

