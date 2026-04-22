#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "client/include/Client.h"
#include "client/include/RespValue.h"

namespace {

using sunkv::client::Client;
using sunkv::client::toDisplayString;

struct HelpEntry {
    const char* cmd;
    const char* text;
    const char* group;
    int group_priority;
};

const std::vector<HelpEntry>& helpTable() {
    static const std::vector<HelpEntry> kHelpTable = {
        {"ALL", "ALL", "meta", 999},
        {"DBSIZE", "DBSIZE", "admin", 0},
        {"DEBUG", "DEBUG INFO | DEBUG RESETSTATS", "admin", 0},
        {"DEL", "DEL <key> [key ...]", "string", 1},
        {"EXISTS", "EXISTS <key> [key ...]", "string", 1},
        {"EXPIRE", "EXPIRE <key> <seconds>", "ttl", 2},
        {"EXEC", "EXEC", "transaction", 6},
        {"FLUSHALL", "FLUSHALL", "admin", 0},
        {"GET", "GET <key>", "string", 1},
        {"HDEL", "HDEL <key> <field> [field ...]", "hash", 5},
        {"HEALTH", "HEALTH", "admin", 0},
        {"HEXISTS", "HEXISTS <key> <field>", "hash", 5},
        {"HGET", "HGET <key> <field>", "hash", 5},
        {"HGETALL", "HGETALL <key>", "hash", 5},
        {"HLEN", "HLEN <key>", "hash", 5},
        {"HSET", "HSET <key> <field> <value> [field value ...]", "hash", 5},
        {"KEYS", "KEYS", "string", 1},
        {"LINDEX", "LINDEX <key> <index>", "list", 3},
        {"LLEN", "LLEN <key>", "list", 3},
        {"LPOP", "LPOP <key>", "list", 3},
        {"LPUSH", "LPUSH <key> <value> [value ...]", "list", 3},
        {"MONITOR", "MONITOR", "admin", 0},
        {"MULTI", "MULTI", "transaction", 6},
        {"PERSIST", "PERSIST <key>", "ttl", 2},
        {"PING", "PING", "admin", 0},
        {"PUBLISH", "PUBLISH <channel> <message>", "pubsub", 7},
        {"PTTL", "PTTL <key>", "ttl", 2},
        {"RPOP", "RPOP <key>", "list", 3},
        {"RPUSH", "RPUSH <key> <value> [value ...]", "list", 3},
        {"SADD", "SADD <key> <member> [member ...]", "set", 4},
        {"SCARD", "SCARD <key>", "set", 4},
        {"SET", "SET <key> <value>", "string", 1},
        {"SISMEMBER", "SISMEMBER <key> <member>", "set", 4},
        {"SMEMBERS", "SMEMBERS <key>", "set", 4},
        {"SNAPSHOT", "SNAPSHOT", "admin", 0},
        {"SREM", "SREM <key> <member> [member ...]", "set", 4},
        {"STATS", "STATS", "admin", 0},
        {"SUBSCRIBE", "SUBSCRIBE <channel> [channel ...]", "pubsub", 7},
        {"TTL", "TTL <key>", "ttl", 2},
        {"UNSUBSCRIBE", "UNSUBSCRIBE [channel ...]", "pubsub", 7},
    };
    return kHelpTable;
}

std::vector<std::string> findHelpCommandByPrefix(const std::string& prefix) {
    std::vector<std::string> matches;
    const auto& table = helpTable();
    auto it = std::lower_bound(
        table.begin(), table.end(), prefix,
        [](const HelpEntry& e, const std::string& p) { return std::string(e.cmd) < p; });

    while (it != table.end()) {
        const std::string cmd = it->cmd;
        if (cmd.rfind(prefix, 0) != 0) {
            break;
        }
        matches.push_back(cmd);
        ++it;
    }
    return matches;
}

const std::string* findExactHelpText(const std::string& cmd) {
    const auto& table = helpTable();
    auto it = std::lower_bound(
        table.begin(), table.end(), cmd,
        [](const HelpEntry& e, const std::string& c) { return std::string(e.cmd) < c; });
    if (it != table.end() && it->cmd == cmd) {
        static std::string out;
        out = it->text;
        return &out;
    }
    return nullptr;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::vector<std::string> splitBySpace(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> out;
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

void printHelp(const std::string& prog) {
    std::cout << "SunKV typed-friendly client\n";
    std::cout << "Usage:\n";
    std::cout << "  " << prog << " <host> <port> [command]\n";
    std::cout << "  " << prog << " --help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " 127.0.0.1 6380 PING\n";
    std::cout << "  " << prog << " 127.0.0.1 6380 \"HSET user:1 name alice age 18\"\n";
    std::cout << "  " << prog << " 127.0.0.1 6380\n\n";
    std::cout << "Typed-friendly commands:\n";
    const auto& table = helpTable();
    for (const auto& entry : table) {
        if (std::string(entry.cmd) == "ALL") continue;
        std::cout << "  " << entry.text << "\n";
    }
    std::cout << "\n";
    std::cout << "Note: unsupported shapes fallback to generic command(args).\n";
}

void printShortHelp(const std::string& prog) {
    std::cout << "Usage: " << prog << " <host> <port> [command]\n";
    std::cout << "Try:  " << prog << " --help\n";
    std::cout << "      " << prog << " 127.0.0.1 6380 \"HELP HSET\"\n";
}

void printCommandHelp(const std::string& rawCmd) {
    const std::string cmd = toUpper(rawCmd);
    if (cmd == "ALL") {
        struct GroupBucket {
            std::string name;
            int priority;
            std::vector<std::string> texts;
        };
        std::vector<GroupBucket> grouped;
        for (const auto& entry : helpTable()) {
            if (std::string(entry.group) == "meta") continue;
            auto it = std::find_if(grouped.begin(), grouped.end(),
                                   [&](const GroupBucket& g) { return g.name == entry.group; });
            if (it == grouped.end()) {
                grouped.push_back(GroupBucket{entry.group, entry.group_priority, {entry.text}});
            } else {
                it->texts.push_back(entry.text);
            }
        }
        std::sort(grouped.begin(), grouped.end(), [](const GroupBucket& a, const GroupBucket& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.name < b.name;
        });
        for (const auto& g : grouped) {
            if (g.texts.empty()) continue;
            std::cout << "[" << g.name << "]\n";
            for (const auto& text : g.texts) {
                std::cout << "  " << text << "\n";
            }
        }
        return;
    }
    if (const std::string* text = findExactHelpText(cmd)) {
        std::cout << *text << "\n";
        return;
    }

    const std::vector<std::string> matches = findHelpCommandByPrefix(cmd);
    if (matches.empty()) {
        std::cout << "Unknown command for help: " << rawCmd << "\n";
        return;
    }
    std::cout << "Did you mean:\n";
    for (const auto& m : matches) {
        std::cout << "  " << m << "\n";
    }
    std::cout << "Try: HELP <exact-command>\n";
}

void printOptionalString(const std::optional<std::string>& v) {
    if (!v.has_value()) {
        std::cout << "(nil)" << std::endl;
        return;
    }
    std::cout << *v << std::endl;
}

void printStringArray(const std::vector<std::string>& items) {
    if (items.empty()) {
        std::cout << "(empty)" << std::endl;
        return;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        std::cout << (i + 1) << ") " << items[i] << std::endl;
    }
}

void printHashArray(const std::vector<std::pair<std::string, std::string>>& kvs) {
    if (kvs.empty()) {
        std::cout << "(empty)" << std::endl;
        return;
    }
    for (const auto& kv : kvs) {
        std::cout << kv.first << " => " << kv.second << std::endl;
    }
}

void printError(const std::string& msg) {
    std::cout << "(error) " << msg << std::endl;
}

bool checkConnected(Client& client) {
    if (client.isConnected()) return true;
    printError("client disconnected");
    return false;
}

bool handleTypedCommand(Client& client, const std::vector<std::string>& args) {
    if (args.empty()) return true;
    if (!checkConnected(client)) return true;

    const std::string cmd = toUpper(args[0]);
    if (cmd == "HELP") {
        if (args.size() == 1) {
            printShortHelp("sunkvClient");
            std::cout << "Hint: HELP ALL\n";
        } else {
            printCommandHelp(args[1]);
        }
        return true;
    }

    if (cmd == "PING") {
        auto r = client.ping();
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "GET" && args.size() == 2) {
        auto r = client.get(args[1]);
        if (!r.ok) printError(r.error.message);
        else printOptionalString(r.value);
        return true;
    }
    if (cmd == "SET" && args.size() == 3) {
        auto r = client.set(args[1], args[2]);
        if (!r.ok) printError(r.error.message);
        else std::cout << "OK" << std::endl;
        return true;
    }
    if (cmd == "DEL" && args.size() >= 2) {
        if (args.size() == 2) {
            auto r = client.del(args[1]);
            if (!r.ok) printError(r.error.message);
            else std::cout << r.value << std::endl;
        } else {
            auto raw = client.command(args);
            if (!raw.ok) printError(raw.error.message);
            else std::cout << toDisplayString(raw.value) << std::endl;
        }
        return true;
    }
    if (cmd == "EXISTS" && args.size() >= 2) {
        std::vector<std::string> keys(args.begin() + 1, args.end());
        auto r = client.exists(keys);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "KEYS") {
        auto r = client.keys();
        if (!r.ok) printError(r.error.message);
        else printStringArray(r.value);
        return true;
    }
    if (cmd == "DBSIZE" && args.size() == 1) {
        auto r = client.dbsize();
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "FLUSHALL" && args.size() == 1) {
        auto r = client.flushall();
        if (!r.ok) printError(r.error.message);
        else std::cout << "OK" << std::endl;
        return true;
    }
    if (cmd == "STATS" && args.size() == 1) {
        auto r = client.stats();
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "MULTI" && args.size() == 1) {
        auto r = client.multi();
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << "OK" << std::endl;
        }
        return true;
    }
    if (cmd == "EXEC" && args.size() == 1) {
        auto r = client.exec();
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << toDisplayString(sunkv::client::RespValue::arrayValue(r.value)) << std::endl;
        }
        return true;
    }
    if (cmd == "DISCARD" && args.size() == 1) {
        auto r = client.discard();
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << "OK" << std::endl;
        }
        return true;
    }
    if (cmd == "PUBLISH" && args.size() == 3) {
        auto r = client.publish(args[1], args[2]);
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << r.value << std::endl;
        }
        return true;
    }
    if (cmd == "SUBSCRIBE" && args.size() >= 2) {
        std::vector<std::string> channels(args.begin() + 1, args.end());
        auto r = client.subscribe(channels);
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << toDisplayString(r.value) << std::endl;
        }
        return true;
    }
    if (cmd == "UNSUBSCRIBE") {
        std::vector<std::string> channels;
        if (args.size() > 1) {
            channels.assign(args.begin() + 1, args.end());
        }
        auto r = client.unsubscribe(channels);
        if (!r.ok) {
            printError(r.error.message);
        } else {
            std::cout << toDisplayString(r.value) << std::endl;
        }
        return true;
    }
    if (cmd == "MONITOR" && args.size() == 1) {
        auto r = client.monitor();
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "SNAPSHOT" && args.size() == 1) {
        auto r = client.snapshot();
        if (!r.ok) printError(r.error.message);
        else std::cout << "OK" << std::endl;
        return true;
    }
    if (cmd == "HEALTH" && args.size() == 1) {
        auto r = client.health();
        if (!r.ok) printError(r.error.message);
        else std::cout << "OK" << std::endl;
        return true;
    }
    if (cmd == "DEBUG" && args.size() == 2 && toUpper(args[1]) == "INFO") {
        auto r = client.debugInfo();
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "DEBUG" && args.size() == 2 && toUpper(args[1]) == "RESETSTATS") {
        auto r = client.debugResetStats();
        if (!r.ok) printError(r.error.message);
        else std::cout << "OK" << std::endl;
        return true;
    }
    if (cmd == "EXPIRE" && args.size() == 3) {
        auto r = client.expire(args[1], std::stoll(args[2]));
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "TTL" && args.size() == 2) {
        auto r = client.ttl(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "PTTL" && args.size() == 2) {
        auto r = client.pttl(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "PERSIST" && args.size() == 2) {
        auto r = client.persist(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if ((cmd == "LPUSH" || cmd == "RPUSH") && args.size() >= 3) {
        std::vector<std::string> values(args.begin() + 2, args.end());
        auto r = (cmd == "LPUSH") ? client.lpush(args[1], values) : client.rpush(args[1], values);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if ((cmd == "LPOP" || cmd == "RPOP") && args.size() == 2) {
        auto r = (cmd == "LPOP") ? client.lpop(args[1]) : client.rpop(args[1]);
        if (!r.ok) printError(r.error.message);
        else printOptionalString(r.value);
        return true;
    }
    if (cmd == "LLEN" && args.size() == 2) {
        auto r = client.llen(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "LINDEX" && args.size() == 3) {
        auto r = client.lindex(args[1], std::stoll(args[2]));
        if (!r.ok) printError(r.error.message);
        else printOptionalString(r.value);
        return true;
    }
    if ((cmd == "SADD" || cmd == "SREM") && args.size() >= 3) {
        std::vector<std::string> members(args.begin() + 2, args.end());
        auto r = (cmd == "SADD") ? client.sadd(args[1], members) : client.srem(args[1], members);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "SCARD" && args.size() == 2) {
        auto r = client.scard(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "SISMEMBER" && args.size() == 3) {
        auto r = client.sismember(args[1], args[2]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "SMEMBERS" && args.size() == 2) {
        auto r = client.smembers(args[1]);
        if (!r.ok) printError(r.error.message);
        else printStringArray(r.value);
        return true;
    }
    if (cmd == "HSET" && args.size() >= 4 && (args.size() % 2 == 0)) {
        std::vector<std::pair<std::string, std::string>> fieldValues;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            fieldValues.emplace_back(args[i], args[i + 1]);
        }
        auto r = client.hset(args[1], fieldValues);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "HGET" && args.size() == 3) {
        auto r = client.hget(args[1], args[2]);
        if (!r.ok) printError(r.error.message);
        else printOptionalString(r.value);
        return true;
    }
    if (cmd == "HDEL" && args.size() >= 3) {
        std::vector<std::string> fields(args.begin() + 2, args.end());
        auto r = client.hdel(args[1], fields);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "HLEN" && args.size() == 2) {
        auto r = client.hlen(args[1]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "HEXISTS" && args.size() == 3) {
        auto r = client.hexists(args[1], args[2]);
        if (!r.ok) printError(r.error.message);
        else std::cout << r.value << std::endl;
        return true;
    }
    if (cmd == "HGETALL" && args.size() == 2) {
        auto r = client.hgetall(args[1]);
        if (!r.ok) printError(r.error.message);
        else printHashArray(r.value);
        return true;
    }

    // typed 未覆盖的参数形态或命令，回退到通用 command
    auto r = client.command(args);
    if (!r.ok) printError(r.error.message);
    else std::cout << toDisplayString(r.value) << std::endl;
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2) {
        const std::string flag = argv[1];
        if (flag == "--help" || flag == "-h" || flag == "help") {
            printHelp(argv[0]);
            return 0;
        }
    }
    if (argc < 3) {
        printShortHelp(argv[0]);
        return 1;
    }

    std::string host = argv[1];
    int port = std::stoi(argv[2]);

    if (argc > 3) {
        std::vector<std::string> args;
        if (argc == 4) {
            args = splitBySpace(argv[3]);
        } else {
            for (int i = 3; i < argc; ++i) {
                args.push_back(argv[i]);
            }
        }
        if (args.empty()) {
            printError("empty command");
            return 2;
        }
        if (toUpper(args[0]) == "HELP") {
            if (args.size() == 1) printShortHelp(argv[0]);
            else printCommandHelp(args[1]);
            return 0;
        }
    }

    Client::Options opts;
    opts.host = host;
    opts.port = static_cast<uint16_t>(port);
    Client client(opts);

    auto conn = client.connect();
    if (!conn.ok) {
        std::cerr << "connect failed: " << conn.error.message << std::endl;
        return 1;
    }

    if (argc > 3) {
        std::vector<std::string> args;
        if (argc == 4) {
            args = splitBySpace(argv[3]);
        } else {
            for (int i = 3; i < argc; ++i) {
                args.push_back(argv[i]);
            }
        }
        handleTypedCommand(client, args);
        return 0;
    }

    std::cout << "SunKV Interactive Client (typed-friendly)" << std::endl;
    std::cout << "Type 'quit' to exit" << std::endl;

    std::string input;
    while (true) {
        std::cout << "SunKV> ";
        if (!std::getline(std::cin, input)) {
            break;
        }
        if (input == "quit" || input == "exit") {
            break;
        }
        if (input.empty()) {
            continue;
        }
        auto args = splitBySpace(input);
        if (args.empty()) {
            continue;
        }
        handleTypedCommand(client, args);
    }

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
