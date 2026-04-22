#include <cassert>
#include <chrono>
#include <thread>

#include "client/include/Client.h"
#include "test/server/server_test_helper.h"

using namespace sunkv::client;
using namespace server_test;

int main() {
    ServerFixture fixture("client_basic_integration_test");
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
    assert(client.isConnected());

    auto p = client.ping();
    assert(p.ok);
    assert(p.value == "PONG");

    auto s = client.set("k1", "v1");
    assert(s.ok);

    auto g = client.get("k1");
    assert(g.ok);
    assert(g.value.has_value());
    assert(*g.value == "v1");

    auto d = client.del("k1");
    assert(d.ok);
    assert(d.value == 1);
    auto exCount = client.exists({"k1"});
    assert(exCount.ok);
    assert(exCount.value == 0);

    auto g2 = client.get("k1");
    assert(g2.ok);
    assert(!g2.value.has_value());

    auto db0 = client.dbsize();
    assert(db0.ok);
    assert(db0.value >= 0);

    auto s2 = client.set("tk", "tv");
    assert(s2.ok);
    auto ex = client.expire("tk", 1);
    assert(ex.ok);
    assert(ex.value == 1);
    auto ttl0 = client.ttl("tk");
    assert(ttl0.ok);
    assert(ttl0.value >= 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    auto ttl1 = client.ttl("tk");
    assert(ttl1.ok);
    assert(ttl1.value == -2);

    auto pp = client.set("pp", "v");
    assert(pp.ok);
    auto exp2 = client.expire("pp", 3);
    assert(exp2.ok && exp2.value == 1);
    auto pttl0 = client.pttl("pp");
    assert(pttl0.ok && pttl0.value >= 0);
    auto pers = client.persist("pp");
    assert(pers.ok && pers.value == 1);
    auto ttlNoExp = client.ttl("pp");
    assert(ttlNoExp.ok && ttlNoExp.value == -1);

    auto l1 = client.lpush("l1", {"a", "b"});
    assert(l1.ok && l1.value == 2);
    auto li = client.lindex("l1", 0);
    assert(li.ok && li.value.has_value() && *li.value == "b");
    auto ll = client.llen("l1");
    assert(ll.ok && ll.value == 2);
    auto lp = client.lpop("l1");
    assert(lp.ok && lp.value.has_value() && *lp.value == "b");
    auto rp = client.rpop("l1");
    assert(rp.ok && rp.value.has_value() && *rp.value == "a");

    auto sAdd = client.sadd("s1", {"x", "y", "x"});
    assert(sAdd.ok && sAdd.value == 2);
    auto sCard = client.scard("s1");
    assert(sCard.ok && sCard.value == 2);
    auto sMem = client.sismember("s1", "x");
    assert(sMem.ok && sMem.value == 1);
    auto sMembers = client.smembers("s1");
    assert(sMembers.ok && sMembers.value.size() == 2);
    auto sRem = client.srem("s1", {"x"});
    assert(sRem.ok && sRem.value == 1);

    auto hSet = client.hset("h1", {{"f1", "v1"}, {"f2", "v2"}});
    assert(hSet.ok && hSet.value == 2);
    auto hGet = client.hget("h1", "f1");
    assert(hGet.ok && hGet.value.has_value() && *hGet.value == "v1");
    auto hExists = client.hexists("h1", "f2");
    assert(hExists.ok && hExists.value == 1);
    auto hLen = client.hlen("h1");
    assert(hLen.ok && hLen.value == 2);
    auto hAll = client.hgetall("h1");
    assert(hAll.ok && hAll.value.size() == 2);
    auto hDel = client.hdel("h1", {"f1"});
    assert(hDel.ok && hDel.value == 1);

    auto ks = client.keys();
    assert(ks.ok);
    int foundPP = 0;
    for (const auto& k : ks.value) {
        if (k == "pp") {
            foundPP = 1;
            break;
        }
    }
    if (foundPP == 0) {
        assert(false && "pp key should be listed by KEYS");
    }

    auto st = client.stats();
    assert(st.ok && !st.value.empty());
    auto mon = client.monitor();
    assert(mon.ok && !mon.value.empty());
    auto dbgInfo = client.debugInfo();
    assert(dbgInfo.ok && !dbgInfo.value.empty());
    auto dbgReset = client.debugResetStats();
    assert(dbgReset.ok);
    auto health = client.health();
    assert(health.ok);
    auto snap = client.snapshot();
    assert(snap.ok);

    // MULTI/EXEC/DISCARD
    auto txBegin = client.multi();
    assert(txBegin.ok);
    auto txQueueSet = client.command({"SET", "tx:k", "v1"});
    assert(txQueueSet.ok);
    assert(txQueueSet.value.type == RespType::SimpleString);
    assert(txQueueSet.value.str == "QUEUED");
    auto txQueueGet = client.command({"GET", "tx:k"});
    assert(txQueueGet.ok);
    assert(txQueueGet.value.type == RespType::SimpleString);
    assert(txQueueGet.value.str == "QUEUED");
    auto txExec = client.exec();
    assert(txExec.ok);
    assert(txExec.value.size() == 2);
    assert(txExec.value[0].type == RespType::SimpleString);
    assert(txExec.value[0].str == "OK");
    assert(txExec.value[1].type == RespType::BulkString);
    assert(txExec.value[1].str == "v1");
    auto txDiscardErr = client.discard();
    assert(!txDiscardErr.ok);

    auto txBegin2 = client.multi();
    assert(txBegin2.ok);
    auto txQueue2 = client.command({"SET", "tx:discard", "x"});
    assert(txQueue2.ok);
    assert(txQueue2.value.type == RespType::SimpleString);
    assert(txQueue2.value.str == "QUEUED");
    auto txDiscard = client.discard();
    assert(txDiscard.ok);
    auto txDiscardGet = client.get("tx:discard");
    assert(txDiscardGet.ok);
    assert(!txDiscardGet.value.has_value());

    // Pub/Sub
    Client subClient(opts);
    auto subConn = subClient.connect();
    assert(subConn.ok);
    auto pubClient = Client(opts);
    auto pubConn = pubClient.connect();
    assert(pubConn.ok);

    auto subAck = subClient.subscribe({"news"});
    assert(subAck.ok);
    assert(subAck.value.type == RespType::Array);
    assert(subAck.value.array.size() == 3);
    assert(subAck.value.array[0].type == RespType::BulkString);
    assert(subAck.value.array[0].str == "subscribe");
    assert(subAck.value.array[1].type == RespType::BulkString);
    assert(subAck.value.array[1].str == "news");

    auto delivered = pubClient.publish("news", "hello");
    assert(delivered.ok);
    assert(delivered.value == 1);

    auto msg = subClient.command({"PING"});
    assert(msg.ok);
    assert(msg.value.type == RespType::Array);
    assert(msg.value.array.size() == 3);
    assert(msg.value.array[0].type == RespType::BulkString);
    assert(msg.value.array[0].str == "message");
    assert(msg.value.array[1].type == RespType::BulkString);
    assert(msg.value.array[1].str == "news");
    assert(msg.value.array[2].type == RespType::BulkString);
    assert(msg.value.array[2].str == "hello");

    auto unsubAck = subClient.unsubscribe({"news"});
    assert(unsubAck.ok);
    assert(unsubAck.value.type == RespType::Array);
    assert(unsubAck.value.array.size() == 3);
    assert(unsubAck.value.array[0].type == RespType::BulkString);
    assert(unsubAck.value.array[0].str == "unsubscribe");
    assert(unsubAck.value.array[1].type == RespType::BulkString);
    assert(unsubAck.value.array[1].str == "news");
    assert(unsubAck.value.array[2].type == RespType::Integer);
    assert(unsubAck.value.array[2].integer == 0);
    subClient.close();
    pubClient.close();

    auto fa = client.flushall();
    assert(fa.ok);
    auto db1 = client.dbsize();
    assert(db1.ok && db1.value == 0);

    client.close();
    assert(!client.isConnected());

    fixture.stop();
    return 0;
}
