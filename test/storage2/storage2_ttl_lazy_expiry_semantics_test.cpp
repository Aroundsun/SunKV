#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"

using sunkv::storage2::StatusCode;

static void assert_ok(StatusCode s) {
    if (s != StatusCode::Ok) {
        assert(false && "status should be Ok");
    }
}

int main() {
    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    // 基础 TTL 语义：missing=-2, no-ttl=-1
    {
        auto t0 = engine.ttl("missing");
        assert_ok(t0.status);
        assert(t0.value == -2);

        assert_ok(engine.set("plain", "v").status);
        auto t1 = engine.ttl("plain");
        assert_ok(t1.status);
        assert(t1.value == -1);
    }

    // persist 语义：去掉 TTL 后返回 1，再次 persist 返回 0
    {
        assert_ok(engine.set("pkey", "v").status);
        auto ex = engine.expire("pkey", 2);
        assert_ok(ex.status);
        assert(ex.value == 1);

        auto p1 = engine.persist("pkey");
        assert_ok(p1.status);
        assert(p1.value == 1);

        auto t = engine.ttl("pkey");
        assert_ok(t.status);
        assert(t.value == -1);

        auto p2 = engine.persist("pkey");
        assert_ok(p2.status);
        assert(p2.value == 0);
    }

    // 惰性过期：keys/dbsize 不主动清理，访问后触发删除
    {
        assert_ok(engine.set("lazy", "v").status);
        auto ex = engine.expire("lazy", 1);
        assert_ok(ex.status);
        assert(ex.value == 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        // 当前实现 keys/dbsize 不做过期过滤（由访问时惰性删除）
        auto ks_before = engine.keys();
        assert_ok(ks_before.status);
        bool seen_lazy = false;
        for (const auto& k : ks_before.value) {
            if (k == "lazy") {
                seen_lazy = true;
                break;
            }
        }
        if (!seen_lazy) {
            assert(false && "lazy key should remain before lazy eviction access");
        }

        auto sz_before = engine.dbsize();
        assert_ok(sz_before.status);
        assert(sz_before.value >= 1);

        // get 触发惰性删除
        auto g = engine.get("lazy");
        assert_ok(g.status);
        assert(!g.value.has_value());
        assert(!g.mutations.empty());
        assert(g.mutations.back().type == sunkv::storage2::MutationType::DelKey);
        assert(g.mutations.back().key == "lazy");

        auto ex2 = engine.exists("lazy");
        assert_ok(ex2.status);
        assert(ex2.value == 0);
    }

    // 参数校验：expire 负数应报 InvalidArg
    {
        assert_ok(engine.set("neg", "v").status);
        auto e = engine.expire("neg", -1);
        assert(e.status == StatusCode::InvalidArg);
    }

    std::cout << "storage2_ttl_lazy_expiry_semantics_test passed." << std::endl;
    return 0;
}
