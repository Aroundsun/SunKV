#include "ArrayCmdDispatch.h"
#include "Server.h"
#include "../network/TcpConnection.h"
#include "../protocol/RESPSerializer.h"
#include "../protocol/RESPType.h"
#include "../common/MemoryPool.h"
#include "../storage2/api/StatusCode.h"
#include "../storage2/engine/Time.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <unordered_map>

struct ArrayCmdDispatchCtx {
    Server& server;
    std::shared_ptr<TcpConnection> conn;
    const std::vector<RESPValue::Ptr>& cmd_array;

    void send_wrongtype() const {
        auto error = RESPSerializer::serializeError("WRONGTYPE Operation against a key holding the wrong kind of value");
        conn->send(error.data(), error.size());
    }
    bool require_bulk(size_t idx) const {
        return idx < cmd_array.size() && cmd_array[idx] && cmd_array[idx]->getType() == RESPType::BULK_STRING;
    }
    std::string bulk_value(size_t idx) const {
        return static_cast<RESPBulkString*>(cmd_array[idx].get())->getValue();
    }
    bool cmdPing();
    bool cmdHealth();
    bool cmdSnapshot();
    bool cmdDbsize();
    bool cmdFlushall();
    bool cmdMonitor();
    bool cmdStats();
    bool cmdSet();
    bool cmdGet();
    bool cmdDel();
    bool cmdExists();
    bool cmdDebug();
    bool cmdKeys();
    bool cmdLpush();
    bool cmdRpush();
    bool cmdLpop();
    bool cmdRpop();
    bool cmdLlen();
    bool cmdLindex();
    bool cmdSadd();
    bool cmdSrem();
    bool cmdSmembers();
    bool cmdScard();
    bool cmdSismember();
    bool cmdHset();
    bool cmdHget();
    bool cmdHdel();
    bool cmdHgetall();
    bool cmdHlen();
    bool cmdHexists();
    bool cmdExpire();
    bool cmdTtl();
    bool cmdPttl();
    bool cmdPersist();
};

bool ArrayCmdDispatchCtx::cmdPing() {
    if (cmd_array.size() != 1) return false;
    conn->send(RESPSerializer::kSimpleStringPong.data(), RESPSerializer::kSimpleStringPong.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHealth() {
    if (cmd_array.size() != 1) return false;
    bool healthy = server.isRunning() && !server.isStopping();
    if (healthy) {
        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
    } else {
        auto err = RESPSerializer::serializeError("UNHEALTHY");
        conn->send(err.data(), err.size());
    }
    return true;
}
bool ArrayCmdDispatchCtx::cmdSnapshot() {
    if (cmd_array.size() != 1) return false;
    if (server.create_multi_type_snapshot()) {
        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
    } else {
        auto error = RESPSerializer::serializeError("Snapshot creation failed");
        conn->send(error.data(), error.size());
    }
    return true;
}
bool ArrayCmdDispatchCtx::cmdDbsize() {
    if (cmd_array.size() != 1) return false;
    int64_t size = 0;
    if (server.storage2_.api) {
        auto r = server.storage2_.api->dbsize();
        if (r.status == sunkv::storage2::StatusCode::Ok) size = r.value;
    }
    auto response = RESPSerializer::serializeInteger(size);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdFlushall() {
    if (cmd_array.size() != 1) return false;
    if (server.storage2_.engine) {
        server.storage2_.engine->loadSnapshot({});
        if (server.storage2_.orchestrator) {
            sunkv::storage2::Mutation m;
            m.type = sunkv::storage2::MutationType::ClearAll;
            m.ts_us = sunkv::storage2::nowEpochUs();
            server.storage2_.orchestrator->submit({m});
        }
    }
    conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdMonitor() {
    if (cmd_array.size() != 1) return false;
    auto resp = RESPSerializer::serializeBulkString(server.buildStatsReport());
    conn->send(resp.data(), resp.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdStats() {
    if (cmd_array.size() != 1) return false;
    auto resp = RESPSerializer::serializeBulkString(server.buildStatsReport());
    conn->send(resp.data(), resp.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdSet() {
    if (cmd_array.size() < 3) return false;
    if (!(require_bulk(1) && require_bulk(2))) return false;
    std::string key = bulk_value(1);
    std::string value = bulk_value(2);
    if (server.storage2_.api) {
        auto r = server.storage2_.api->set(key, value);
        if (r.status == sunkv::storage2::StatusCode::QuotaExceeded) {
            auto err = RESPSerializer::serializeError("OOM maxmemory");
            conn->send(err.data(), err.size());
            return true;
        }
    }
    conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdGet() {
    if (cmd_array.size() < 2) return false;
    if (!require_bulk(1)) return false;
    std::string key = bulk_value(1);
    if (!server.storage2_.api) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto r = server.storage2_.api->get(key);
    if (r.status != sunkv::storage2::StatusCode::Ok || !r.value.has_value()) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto response = RESPSerializer::serializeBulkString(*r.value);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdDel() {
    if (cmd_array.size() < 2) return false;
    int deleted_count = 0;
    for (size_t i = 1; i < cmd_array.size(); ++i) {
        if (require_bulk(i)) {
            std::string key = bulk_value(i);
            if (server.storage2_.api) {
                auto r = server.storage2_.api->del(key);
                if (r.status == sunkv::storage2::StatusCode::Ok && r.value > 0) {
                    deleted_count += static_cast<int>(r.value);
                }
            }
        }
    }
    auto response = RESPSerializer::serializeInteger(deleted_count);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdExists() {
    if (cmd_array.size() < 2) return false;
    int exists_count = 0;
    for (size_t i = 1; i < cmd_array.size(); ++i) {
        if (require_bulk(i)) {
            std::string key = bulk_value(i);
            if (server.storage2_.api) {
                auto r = server.storage2_.api->exists(key);
                if (r.status == sunkv::storage2::StatusCode::Ok && r.value > 0) {
                    exists_count += static_cast<int>(r.value);
                }
            }
        }
    }
    auto response = RESPSerializer::serializeInteger(exists_count);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdDebug() {
    if (cmd_array.size() < 2 || !cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) {
        auto err = RESPSerializer::serializeError("ERR wrong number of arguments for 'DEBUG' command");
        conn->send(err.data(), err.size());
        return true;
    }
    auto* subcmd_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string subcmd = subcmd_bulk->getValue();
    std::transform(subcmd.begin(), subcmd.end(), subcmd.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (subcmd == "INFO") {
        auto resp = RESPSerializer::serializeBulkString(server.buildStatsReport());
        conn->send(resp.data(), resp.size());
        return true;
    }
    if (subcmd == "RESETSTATS") {
        ThreadLocalBufferPool::instance().resetStats();
        conn->send(RESPSerializer::kSimpleStringOk.data(), RESPSerializer::kSimpleStringOk.size());
        return true;
    }
    auto err = RESPSerializer::serializeError("ERR unknown DEBUG subcommand");
    conn->send(err.data(), err.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdKeys() {
    std::string keys_array = "*";
    std::vector<std::string> keys;
    if (server.storage2_.api) {
        auto r = server.storage2_.api->keys();
        if (r.status == sunkv::storage2::StatusCode::Ok) {
            keys = std::move(r.value);
        }
    }
    keys_array += std::to_string(keys.size()) + "\r\n";
    for (const auto& key : keys) {
        keys_array += "$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
    }
    conn->send(keys_array.data(), keys_array.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdLpush() {
    if (cmd_array.size() < 3) return false;
    // 如果命令的数组值不为空，则获取命令的第一个值
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    // 创建一个值的数组
    std::vector<std::string> values;
    // 预分配值的数组大小
    values.reserve(cmd_array.size() - 2);
    // 遍历命令的数组值
    for (size_t i = 2; i < cmd_array.size(); ++i) {
        // 如果命令的数组值不为空，则获取命令的第一个值
        if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
            // 获取命令的第一个值
            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
            // 将命令的第一个值添加到值的数组
            values.push_back(value_bulk->getValue());
        }
    }
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->lpush(key, values);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdRpush() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    std::vector<std::string> values;
    values.reserve(cmd_array.size() - 2);
    for (size_t i = 2; i < cmd_array.size(); ++i) {
        if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
            values.push_back(value_bulk->getValue());
        }
    }
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->rpush(key, values);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdLpop() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto r = server.storage2_.api->lpop(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    if (r.status != sunkv::storage2::StatusCode::Ok || !r.value.has_value()) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto response = RESPSerializer::serializeBulkString(*r.value);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdRpop() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto r = server.storage2_.api->rpop(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    if (r.status != sunkv::storage2::StatusCode::Ok || !r.value.has_value()) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto response = RESPSerializer::serializeBulkString(*r.value);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdLlen() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->llen(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdLindex() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING ||
        !cmd_array[2] || cmd_array[2]->getType() != RESPType::BULK_STRING) {
        return false;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    auto* index_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
    std::string key = key_bulk->getValue();
    int64_t index = 0;
    try {
        index = std::stoll(index_bulk->getValue());
    } catch (...) {
        auto error = RESPSerializer::serializeError("ERR value is not an integer or out of range");
        conn->send(error.data(), error.size());
        return true;
    }
    if (!server.storage2_.api) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto r = server.storage2_.api->lindex(key, index);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    if (r.status != sunkv::storage2::StatusCode::Ok || !r.value.has_value()) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto response = RESPSerializer::serializeBulkString(*r.value);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdSadd() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    std::vector<std::string> members;
    members.reserve(cmd_array.size() - 2);
    for (size_t i = 2; i < cmd_array.size(); ++i) {
        if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
            members.push_back(value_bulk->getValue());
        }
    }
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->sadd(key, members);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdSrem() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    std::vector<std::string> members;
    members.reserve(cmd_array.size() - 2);
    for (size_t i = 2; i < cmd_array.size(); ++i) {
        if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
            auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
            members.push_back(value_bulk->getValue());
        }
    }
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->srem(key, members);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdSmembers() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        conn->send("*0\r\n", 4);
        return true;
    }
    auto r = server.storage2_.api->smembers(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    const auto& members = (r.status == sunkv::storage2::StatusCode::Ok) ? r.value : std::vector<std::string>{};
    std::string members_array = "*" + std::to_string(members.size()) + "\r\n";
    for (const auto& member : members) {
        members_array += "$" + std::to_string(member.length()) + "\r\n" + member + "\r\n";
    }
    conn->send(members_array.data(), members_array.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdScard() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->scard(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdSismember() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING ||
        !cmd_array[2] || cmd_array[2]->getType() != RESPType::BULK_STRING) {
        return false;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    auto* member_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
    std::string key = key_bulk->getValue();
    std::string member = member_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->sismember(key, member);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHset() {
    if (cmd_array.size() < 4) return false;
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) {
        auto error = RESPSerializer::serializeError("ERR wrong number of arguments for 'hset' command");
        conn->send(error.data(), error.size());
        return true;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (((cmd_array.size() - 2) % 2) != 0) {
        auto error = RESPSerializer::serializeError("ERR wrong number of arguments for 'hset' command");
        conn->send(error.data(), error.size());
        return true;
    }
    int64_t added_total = 0;
    for (size_t i = 2; i + 1 < cmd_array.size(); i += 2) {
        if (!cmd_array[i] || cmd_array[i]->getType() != RESPType::BULK_STRING ||
            !cmd_array[i + 1] || cmd_array[i + 1]->getType() != RESPType::BULK_STRING) {
            continue;
        }
        auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
        auto* value_bulk = static_cast<RESPBulkString*>(cmd_array[i + 1].get());
        auto r = server.storage2_.api->hset(key, field_bulk->getValue(), value_bulk->getValue());
        if (r.status == sunkv::storage2::StatusCode::WrongType) {
            send_wrongtype();
            return true;
        }
        if (r.status == sunkv::storage2::StatusCode::Ok) {
            added_total += r.value;
        }
    }
    auto response = RESPSerializer::serializeInteger(added_total);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHget() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING ||
        !cmd_array[2] || cmd_array[2]->getType() != RESPType::BULK_STRING) {
        return false;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
    std::string key = key_bulk->getValue();
    std::string field = field_bulk->getValue();
    if (!server.storage2_.api) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto r = server.storage2_.api->hget(key, field);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    if (r.status != sunkv::storage2::StatusCode::Ok || !r.value.has_value()) {
        conn->send(RESPSerializer::kNullBulkString.data(), RESPSerializer::kNullBulkString.size());
        return true;
    }
    auto response = RESPSerializer::serializeBulkString(*r.value);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHdel() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    std::vector<std::string> fields;
    fields.reserve(cmd_array.size() - 2);
    for (size_t i = 2; i < cmd_array.size(); ++i) {
        if (cmd_array[i] && cmd_array[i]->getType() == RESPType::BULK_STRING) {
            auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[i].get());
            fields.push_back(field_bulk->getValue());
        }
    }
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->hdel(key, fields);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHgetall() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        conn->send("*0\r\n", 4);
        return true;
    }
    auto r = server.storage2_.api->hgetall(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    const auto& kvs = (r.status == sunkv::storage2::StatusCode::Ok) ? r.value
                                                                    : std::vector<std::pair<std::string, std::string>>{};
    std::string hash_array = "*" + std::to_string(kvs.size() * 2) + "\r\n";
    for (const auto& pair : kvs) {
        hash_array += "$" + std::to_string(pair.first.length()) + "\r\n" + pair.first + "\r\n";
        hash_array += "$" + std::to_string(pair.second.length()) + "\r\n" + pair.second + "\r\n";
    }
    conn->send(hash_array.data(), hash_array.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHlen() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->hlen(key);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdHexists() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING ||
        !cmd_array[2] || cmd_array[2]->getType() != RESPType::BULK_STRING) {
        return false;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    auto* field_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
    std::string key = key_bulk->getValue();
    std::string field = field_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->hexists(key, field);
    if (r.status == sunkv::storage2::StatusCode::WrongType) {
        send_wrongtype();
        return true;
    }
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdExpire() {
    if (cmd_array.size() < 3) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING ||
        !cmd_array[2] || cmd_array[2]->getType() != RESPType::BULK_STRING) {
        return false;
    }
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    auto* ttl_bulk = static_cast<RESPBulkString*>(cmd_array[2].get());
    std::string key = key_bulk->getValue();
    try {
        int64_t ttl_seconds = std::stoll(ttl_bulk->getValue());
        if (ttl_seconds > server.maxTtlSeconds()) {
            auto error = RESPSerializer::serializeError("ERR TTL exceeds server max_ttl_seconds");
            conn->send(error.data(), error.size());
            return true;
        }
        if (!server.storage2_.api) {
            auto response = RESPSerializer::serializeInteger(0);
            conn->send(response.data(), response.size());
            return true;
        }
        auto r = server.storage2_.api->expire(key, ttl_seconds);
        auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
        conn->send(response.data(), response.size());
        return true;
    } catch (const std::exception& e) {
        auto error = RESPSerializer::serializeError("Invalid TTL value");
        conn->send(error.data(), error.size());
        return true;
    }
}
bool ArrayCmdDispatchCtx::cmdTtl() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    int64_t remaining_ttl = -2;
    if (server.storage2_.api) {
        auto r = server.storage2_.api->ttl(key);
        if (r.status == sunkv::storage2::StatusCode::Ok) remaining_ttl = r.value;
    }
    auto response = RESPSerializer::serializeInteger(remaining_ttl);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdPttl() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    int64_t remaining = -2;
    if (server.storage2_.api) {
        auto r = server.storage2_.api->pttl(key);
        if (r.status == sunkv::storage2::StatusCode::Ok) remaining = r.value;
    }
    auto response = RESPSerializer::serializeInteger(remaining);
    conn->send(response.data(), response.size());
    return true;
}
bool ArrayCmdDispatchCtx::cmdPersist() {
    if (cmd_array.size() < 2) return false;
    if (!cmd_array[1] || cmd_array[1]->getType() != RESPType::BULK_STRING) return false;
    auto* key_bulk = static_cast<RESPBulkString*>(cmd_array[1].get());
    std::string key = key_bulk->getValue();
    if (!server.storage2_.api) {
        auto response = RESPSerializer::serializeInteger(0);
        conn->send(response.data(), response.size());
        return true;
    }
    auto r = server.storage2_.api->persist(key);
    auto response = RESPSerializer::serializeInteger(r.status == sunkv::storage2::StatusCode::Ok ? r.value : 0);
    conn->send(response.data(), response.size());
    return true;
}

namespace {

const std::unordered_map<std::string, std::function<bool(ArrayCmdDispatchCtx&)>>& array_cmd_dispatch_table() {
    static const std::unordered_map<std::string, std::function<bool(ArrayCmdDispatchCtx&)>> table = [] {
        std::unordered_map<std::string, std::function<bool(ArrayCmdDispatchCtx&)>> m;
        m.reserve(48);
        m.emplace("PING", [](ArrayCmdDispatchCtx& c) { return c.cmdPing(); });
        m.emplace("HEALTH", [](ArrayCmdDispatchCtx& c) { return c.cmdHealth(); });
        m.emplace("SNAPSHOT", [](ArrayCmdDispatchCtx& c) { return c.cmdSnapshot(); });
        m.emplace("DBSIZE", [](ArrayCmdDispatchCtx& c) { return c.cmdDbsize(); });
        m.emplace("FLUSHALL", [](ArrayCmdDispatchCtx& c) { return c.cmdFlushall(); });
        m.emplace("MONITOR", [](ArrayCmdDispatchCtx& c) { return c.cmdMonitor(); });
        m.emplace("STATS", [](ArrayCmdDispatchCtx& c) { return c.cmdStats(); });
        m.emplace("SET", [](ArrayCmdDispatchCtx& c) { return c.cmdSet(); });
        m.emplace("GET", [](ArrayCmdDispatchCtx& c) { return c.cmdGet(); });
        m.emplace("DEL", [](ArrayCmdDispatchCtx& c) { return c.cmdDel(); });
        m.emplace("EXISTS", [](ArrayCmdDispatchCtx& c) { return c.cmdExists(); });
        m.emplace("DEBUG", [](ArrayCmdDispatchCtx& c) { return c.cmdDebug(); });
        m.emplace("KEYS", [](ArrayCmdDispatchCtx& c) { return c.cmdKeys(); });
        m.emplace("LPUSH", [](ArrayCmdDispatchCtx& c) { return c.cmdLpush(); });
        m.emplace("RPUSH", [](ArrayCmdDispatchCtx& c) { return c.cmdRpush(); });
        m.emplace("LPOP", [](ArrayCmdDispatchCtx& c) { return c.cmdLpop(); });
        m.emplace("RPOP", [](ArrayCmdDispatchCtx& c) { return c.cmdRpop(); });
        m.emplace("LLEN", [](ArrayCmdDispatchCtx& c) { return c.cmdLlen(); });
        m.emplace("LINDEX", [](ArrayCmdDispatchCtx& c) { return c.cmdLindex(); });
        m.emplace("SADD", [](ArrayCmdDispatchCtx& c) { return c.cmdSadd(); });
        m.emplace("SREM", [](ArrayCmdDispatchCtx& c) { return c.cmdSrem(); });
        m.emplace("SMEMBERS", [](ArrayCmdDispatchCtx& c) { return c.cmdSmembers(); });
        m.emplace("SCARD", [](ArrayCmdDispatchCtx& c) { return c.cmdScard(); });
        m.emplace("SISMEMBER", [](ArrayCmdDispatchCtx& c) { return c.cmdSismember(); });
        m.emplace("HSET", [](ArrayCmdDispatchCtx& c) { return c.cmdHset(); });
        m.emplace("HGET", [](ArrayCmdDispatchCtx& c) { return c.cmdHget(); });
        m.emplace("HDEL", [](ArrayCmdDispatchCtx& c) { return c.cmdHdel(); });
        m.emplace("HGETALL", [](ArrayCmdDispatchCtx& c) { return c.cmdHgetall(); });
        m.emplace("HLEN", [](ArrayCmdDispatchCtx& c) { return c.cmdHlen(); });
        m.emplace("HEXISTS", [](ArrayCmdDispatchCtx& c) { return c.cmdHexists(); });
        m.emplace("EXPIRE", [](ArrayCmdDispatchCtx& c) { return c.cmdExpire(); });
        m.emplace("TTL", [](ArrayCmdDispatchCtx& c) { return c.cmdTtl(); });
        m.emplace("PTTL", [](ArrayCmdDispatchCtx& c) { return c.cmdPttl(); });
        m.emplace("PERSIST", [](ArrayCmdDispatchCtx& c) { return c.cmdPersist(); });
        return m;
    }();
    return table;
}

} // namespace

bool dispatchArrayCommandsLookup(Server& server,
                                 const std::shared_ptr<TcpConnection>& conn,
                                 const std::string& cmd_name,
                                 const std::vector<RESPValue::Ptr>& cmd_array) {
    ArrayCmdDispatchCtx ctx{server, conn, cmd_array};
    const auto& tab = array_cmd_dispatch_table();
    if (auto it = tab.find(cmd_name); it != tab.end()) {
        if (it->second(ctx)) return true;
    }
    return false;
}
