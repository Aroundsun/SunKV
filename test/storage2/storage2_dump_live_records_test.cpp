#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
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

    assert_ok(engine.set("live_str", "v").status);
    assert_ok(engine.hset("live_hash", "f", "x").status);
    assert_ok(engine.set("expiring", "z").status);
    auto ex = engine.expire("expiring", 1);
    assert_ok(ex.status);
    assert(ex.value == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // dumpAllLiveRecords 应过滤掉过期 key，并触发惰性清理。
    auto records = engine.dumpAllLiveRecords();
    bool has_live_str = false;
    bool has_live_hash = false;
    bool has_expiring = false;
    for (const auto& kv : records) {
        if (kv.first == "live_str") {
            has_live_str = true;
            assert(kv.second.value.type == DataType::STRING);
            assert(kv.second.value.string_value == "v");
        } else if (kv.first == "live_hash") {
            has_live_hash = true;
            assert(kv.second.value.type == DataType::HASH);
            assert(kv.second.value.hash_value.count("f") == 1);
        } else if (kv.first == "expiring") {
            has_expiring = true;
        }
    }

    if (!has_live_str) {
        assert(false && "live_str should exist in dumpAllLiveRecords");
    }
    if (!has_live_hash) {
        assert(false && "live_hash should exist in dumpAllLiveRecords");
    }
    if (has_expiring) {
        assert(false && "expired key should be filtered from dumpAllLiveRecords");
    }

    // 过期 key 应已被 dump 过程惰性删除
    auto e = engine.exists("expiring");
    assert_ok(e.status);
    assert(e.value == 0);

    std::cout << "storage2_dump_live_records_test passed." << std::endl;
    return 0;
}
