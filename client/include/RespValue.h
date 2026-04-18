#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sunkv::client {

enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    NullBulkString,
};

struct RespValue {
    RespType type{RespType::SimpleString};
    std::string str;
    int64_t integer{0};
    std::vector<RespValue> array;

    static RespValue simpleString(const std::string& v) {
        RespValue out;
        out.type = RespType::SimpleString;
        out.str = v;
        return out;
    }

    static RespValue error(const std::string& v) {
        RespValue out;
        out.type = RespType::Error;
        out.str = v;
        return out;
    }

    static RespValue integerValue(int64_t v) {
        RespValue out;
        out.type = RespType::Integer;
        out.integer = v;
        return out;
    }

    static RespValue bulkString(const std::string& v) {
        RespValue out;
        out.type = RespType::BulkString;
        out.str = v;
        return out;
    }

    static RespValue nullBulkString() {
        RespValue out;
        out.type = RespType::NullBulkString;
        return out;
    }

    static RespValue arrayValue(std::vector<RespValue> elems) {
        RespValue out;
        out.type = RespType::Array;
        out.array = std::move(elems);
        return out;
    }
};

inline std::string toDisplayString(const RespValue& v) {
    switch (v.type) {
        case RespType::SimpleString:
            return v.str;
        case RespType::Error:
            return "(error) " + v.str;
        case RespType::Integer:
            return "(integer) " + std::to_string(v.integer);
        case RespType::BulkString:
            return v.str;
        case RespType::NullBulkString:
            return "(nil)";
        case RespType::Array: {
            std::string out = "[";
            for (size_t i = 0; i < v.array.size(); ++i) {
                if (i > 0) out += ", ";
                out += toDisplayString(v.array[i]);
            }
            out += "]";
            return out;
        }
    }
    return {};
}

} // namespace sunkv::client
