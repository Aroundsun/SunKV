#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"

using sunkv::storage2::StatusCode;

static void assert_ok(StatusCode s) {
    (void)s;
    assert(s == StatusCode::Ok);
}

int main() {
    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    // LIST
    {
        auto r = engine.lpush("l1", {"a", "b"});
        assert_ok(r.status);
        assert(r.value == 2);

        auto len = engine.llen("l1");
        assert_ok(len.status);
        assert(len.value == 2);

        auto v1 = engine.lpop("l1");
        assert_ok(v1.status);
        assert(v1.value.has_value());
        assert(*v1.value == "b"); // LPUSH a b => list: b a

        auto v2 = engine.rpop("l1");
        assert_ok(v2.status);
        assert(v2.value.has_value());
        assert(*v2.value == "a");

        auto v3 = engine.lpop("l1");
        assert_ok(v3.status);
        assert(!v3.value.has_value());

        auto ex = engine.exists("l1");
        assert_ok(ex.status);
        assert(ex.value == 0); // 空 list 删除 key，保持与 Redis 一致
    }

    // SET
    {
        auto a1 = engine.sadd("s1", {"x", "y", "x"});
        assert_ok(a1.status);
        assert(a1.value == 2);

        auto card = engine.scard("s1");
        assert_ok(card.status);
        assert(card.value == 2);

        auto ismem = engine.sismember("s1", "x");
        assert_ok(ismem.status);
        assert(ismem.value == 1);

        auto rem = engine.srem("s1", {"x", "z"});
        assert_ok(rem.status);
        assert(rem.value == 1);

        auto rem2 = engine.srem("s1", {"y"});
        assert_ok(rem2.status);
        assert(rem2.value == 1);
        auto ex = engine.exists("s1");
        assert_ok(ex.status);
        assert(ex.value == 0); // 空 set 删除 key
    }

    // HASH
    {
        auto h1 = engine.hset("h1", "f1", "v1");
        assert_ok(h1.status);
        assert(h1.value == 1);

        auto h2 = engine.hset("h1", "f1", "v2");
        assert_ok(h2.status);
        assert(h2.value == 0);

        auto g1 = engine.hget("h1", "f1");
        assert_ok(g1.status);
        assert(g1.value.has_value());
        assert(*g1.value == "v2");

        auto ex = engine.hexists("h1", "f1");
        assert_ok(ex.status);
        assert(ex.value == 1);

        auto all = engine.hgetall("h1");
        assert_ok(all.status);
        assert(all.value.size() == 1);
        assert(all.value[0].first == "f1");
        assert(all.value[0].second == "v2");

        auto del = engine.hdel("h1", {"f1", "f2"});
        assert_ok(del.status);
        assert(del.value == 1);

        auto ex2 = engine.exists("h1");
        assert_ok(ex2.status);
        assert(ex2.value == 0); // 空 hash 删除 key
    }

    // EXPIRE
    {
        assert_ok(engine.set("exp0", "v").status);
        auto e0 = engine.expire("exp0", 0);
        assert_ok(e0.status);
        assert(e0.value == 1);
        auto ex = engine.exists("exp0");
        assert_ok(ex.status);
        assert(ex.value == 0);
    }

    // WrongType 基本检查
    {
        assert_ok(engine.set("k", "v").status);
        auto r1 = engine.lpush("k", {"x"});
        assert(r1.status == StatusCode::WrongType);
        auto r2 = engine.sadd("k", {"x"});
        assert(r2.status == StatusCode::WrongType);
        auto r3 = engine.hset("k", "f", "v");
        assert(r3.status == StatusCode::WrongType);
    }

    std::cout << "storage2_multitype_ops_test passed." << std::endl;
    return 0;
}

