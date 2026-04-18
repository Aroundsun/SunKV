#include <cassert>

#include "client/include/Client.h"
#include "test/server/server_test_helper.h"

using namespace sunkv::client;
using namespace server_test;

int main() {
    ServerFixture fixture("client_pipeline_integration_test");
    fixture.start();

    Client::Options opts;
    opts.host = fixture.host;
    opts.port = static_cast<uint16_t>(fixture.port);
    opts.connect_timeout_ms = 1000;
    opts.read_timeout_ms = 2000;
    opts.write_timeout_ms = 2000;

    Client client(opts);
    auto c = client.connect();
    assert(c.ok);

    std::vector<std::vector<std::string>> cmds{
        {"SET", "p1", "v1"},
        {"GET", "p1"},
        {"DEL", "p1"},
        {"GET", "p1"},
        {"PING"},
    };
    auto rp = client.pipeline(cmds);
    assert(rp.ok);
    assert(rp.value.size() == cmds.size());

    assert(rp.value[0].type == RespType::SimpleString && rp.value[0].str == "OK");
    assert(rp.value[1].type == RespType::BulkString && rp.value[1].str == "v1");
    assert(rp.value[2].type == RespType::Integer && rp.value[2].integer == 1);
    assert(rp.value[3].type == RespType::NullBulkString);
    assert(rp.value[4].type == RespType::SimpleString && rp.value[4].str == "PONG");

    // pipeline 中含错误命令：客户端应返回该 RESP Error 值，而不是整体失败
    std::vector<std::vector<std::string>> badCmds{
        {"BADCMD"},
        {"PING"},
    };
    auto rp2 = client.pipeline(badCmds);
    assert(rp2.ok);
    assert(rp2.value.size() == badCmds.size());
    assert(rp2.value[0].type == RespType::Error);
    assert(!rp2.value[0].str.empty());
    assert(rp2.value[1].type == RespType::SimpleString && rp2.value[1].str == "PONG");

    // 严格模式：遇到服务端 error 立即失败，并关闭连接避免残留响应污染后续会话。
    Client strictClient(opts);
    auto c2 = strictClient.connect();
    assert(c2.ok);
    Client::PipelineOptions popts;
    popts.fail_on_server_error = true;
    popts.close_on_server_error = true;
    auto rp3 = strictClient.pipeline(badCmds, popts);
    assert(!rp3.ok);
    assert(rp3.error.code == ErrorCode::ServerError);
    assert(!strictClient.isConnected());

    // typed pipeline helpers
    auto mset = client.mset({{"m1", "x"}, {"m2", "y"}});
    assert(mset.ok);
    auto mget = client.mget({"m1", "m2", "m3"});
    assert(mget.ok);
    assert(mget.value.size() == 3);
    assert(mget.value[0].has_value() && *mget.value[0] == "x");
    assert(mget.value[1].has_value() && *mget.value[1] == "y");
    assert(!mget.value[2].has_value());

    client.close();
    fixture.stop();
    return 0;
}
