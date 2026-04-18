#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

#include "storage2/backend/InMemoryBackend.h"
#include "storage2/engine/StorageEngine.h"

int main() {
    auto backend = std::make_unique<sunkv::storage2::InMemoryBackend>();
    sunkv::storage2::StorageEngine engine(std::move(backend));

    {
        auto r = engine.set("k1", "v1");
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        assert(r.value == true);
        assert(!r.mutations.empty());
    }
    {
        auto r = engine.get("k1");
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        assert(r.value.has_value() && r.value.value() == "v1");
    }
    {
        auto r = engine.exists("k1");
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        assert(r.value == 1);
    }
    {
        auto r = engine.expire("k1", 1);
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        assert(r.value == 1);
    }
    {
        auto r = engine.set("k_exp0", "v");
        assert(r.status == sunkv::storage2::StatusCode::Ok);
        auto e0 = engine.expire("k_exp0", 0);
        assert(e0.status == sunkv::storage2::StatusCode::Ok);
        assert(e0.value == 1);
        auto ex = engine.exists("k_exp0");
        assert(ex.status == sunkv::storage2::StatusCode::Ok);
        assert(ex.value == 0);
    }
    {
        // TTL/PTTL 边界：未到期时不应返回 -2
        auto t = engine.ttl("k1");
        assert(t.status == sunkv::storage2::StatusCode::Ok);
        assert(t.value >= 0);

        auto pt = engine.pttl("k1");
        assert(pt.status == sunkv::storage2::StatusCode::Ok);
        assert(pt.value >= 0);
    }
    {
        // 到期后应返回 -2 且 key 被惰性删除
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        auto t = engine.ttl("k1");
        assert(t.status == sunkv::storage2::StatusCode::Ok);
        assert(t.value == -2);

        auto ex = engine.exists("k1");
        assert(ex.status == sunkv::storage2::StatusCode::Ok);
        assert(ex.value == 0);
    }

    std::cout << "storage2_smoke_test passed." << std::endl;
    return 0;
}

