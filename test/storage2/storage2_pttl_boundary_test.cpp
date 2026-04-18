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

    // 基础边界：不存在=-2，无 TTL=-1
    {
        auto p0 = engine.pttl("missing");
        assert_ok(p0.status);
        assert(p0.value == -2);

        assert_ok(engine.set("plain", "v").status);
        auto p1 = engine.pttl("plain");
        assert_ok(p1.status);
        assert(p1.value == -1);
    }

    // 单调性：在未过期区间，PTTL 应整体递减（允许离散时间步进）
    {
        assert_ok(engine.set("mono", "v").status);
        auto ex = engine.expire("mono", 2);
        assert_ok(ex.status);
        assert(ex.value == 1);

        auto a = engine.pttl("mono");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto b = engine.pttl("mono");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto c = engine.pttl("mono");

        assert_ok(a.status);
        assert_ok(b.status);
        assert_ok(c.status);
        assert(a.value >= 0);
        assert(b.value >= 0);
        assert(c.value >= 0);
        assert(a.value >= b.value);
        assert(b.value >= c.value);
    }

    // 过期后：PTTL 返回 -2，且 key 被惰性删除
    {
        assert_ok(engine.set("exp", "v").status);
        auto ex = engine.expire("exp", 1);
        assert_ok(ex.status);
        assert(ex.value == 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        auto p = engine.pttl("exp");
        assert_ok(p.status);
        assert(p.value == -2);
        assert(!p.mutations.empty());
        assert(p.mutations.back().type == sunkv::storage2::MutationType::DelKey);
        assert(p.mutations.back().key == "exp");

        auto exists = engine.exists("exp");
        assert_ok(exists.status);
        assert(exists.value == 0);
    }

    std::cout << "storage2_pttl_boundary_test passed." << std::endl;
    return 0;
}
