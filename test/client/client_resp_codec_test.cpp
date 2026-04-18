#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "client/src/RespCodec.h"

using namespace sunkv::client;

int main() {
    {
        const std::string s = "+PONG\r\n";
        auto r = parseRespValue(s);
        assert(r.success && r.complete);
        assert(r.value.type == RespType::SimpleString);
        assert(r.value.str == "PONG");
        assert(r.consumed_bytes == s.size());
    }
    {
        const std::string s = ":42\r\n";
        auto r = parseRespValue(s);
        assert(r.success && r.complete);
        assert(r.value.type == RespType::Integer);
        assert(r.value.integer == 42);
    }
    {
        const std::string s = "$3\r\nfoo\r\n";
        auto r = parseRespValue(s);
        assert(r.success && r.complete);
        assert(r.value.type == RespType::BulkString);
        assert(r.value.str == "foo");
    }
    {
        const std::string s = "$-1\r\n";
        auto r = parseRespValue(s);
        assert(r.success && r.complete);
        assert(r.value.type == RespType::NullBulkString);
    }
    {
        const std::string s = "*2\r\n$4\r\nPING\r\n$4\r\ntest\r\n";
        auto r = parseRespValue(s);
        assert(r.success && r.complete);
        assert(r.value.type == RespType::Array);
        assert(r.value.array.size() == 2);
        assert(r.value.array[0].str == "PING");
        assert(r.value.array[1].str == "test");
    }
    {
        const std::string partial = "$5\r\nab";
        auto r = parseRespValue(partial);
        assert(r.success && !r.complete);
    }
    {
        const std::string multi = "+OK\r\n:7\r\n";
        auto r1 = parseRespValue(multi);
        assert(r1.success && r1.complete);
        assert(r1.value.type == RespType::SimpleString);
        assert(r1.value.str == "OK");
        assert(r1.consumed_bytes == 5);

        auto r2 = parseRespValue(std::string_view(multi).substr(r1.consumed_bytes));
        assert(r2.success && r2.complete);
        assert(r2.value.type == RespType::Integer);
        assert(r2.value.integer == 7);
    }
    {
        std::vector<std::string> args{"SET", "k", "v"};
        std::string out = encodeRespArrayCommand(args);
        assert(out == "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$1\r\nv\r\n");
    }
    return 0;
}
