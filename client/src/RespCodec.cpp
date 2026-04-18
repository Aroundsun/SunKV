#include "RespCodec.h"

#include <charconv>

namespace sunkv::client {

namespace {

bool parseInt64(std::string_view s, int64_t* out) {
    if (!out || s.empty()) return false;
    int64_t v = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, v);
    if (ec != std::errc() || ptr != end) return false;
    *out = v;
    return true;
}

RespParseResult parseOne(std::string_view input) {
    RespParseResult r;
    if (input.empty()) {
        r.success = true;
        r.complete = false;
        return r;
    }

    const char type = input[0];
    auto findCRLF = [&](size_t start) -> size_t {
        for (size_t i = start; i + 1 < input.size(); ++i) {
            if (input[i] == '\r' && input[i + 1] == '\n') return i;
        }
        return std::string_view::npos;
    };

    if (type == '+' || type == '-' || type == ':') {
        const size_t end = findCRLF(1);
        if (end == std::string_view::npos) {
            r.success = true;
            r.complete = false;
            return r;
        }
        const std::string_view payload = input.substr(1, end - 1);
        r.consumed_bytes = end + 2;
        r.success = true;
        r.complete = true;
        if (type == '+') {
            r.value = RespValue::simpleString(std::string(payload));
            return r;
        }
        if (type == '-') {
            r.value = RespValue::error(std::string(payload));
            return r;
        }
        int64_t iv = 0;
        if (!parseInt64(payload, &iv)) {
            r.success = false;
            r.error = "invalid integer response";
            return r;
        }
        r.value = RespValue::integerValue(iv);
        return r;
    }

    if (type == '$') {
        const size_t end = findCRLF(1);
        if (end == std::string_view::npos) {
            r.success = true;
            r.complete = false;
            return r;
        }
        int64_t len = 0;
        if (!parseInt64(input.substr(1, end - 1), &len)) {
            r.success = false;
            r.error = "invalid bulk string length";
            return r;
        }
        if (len == -1) {
            r.success = true;
            r.complete = true;
            r.consumed_bytes = end + 2;
            r.value = RespValue::nullBulkString();
            return r;
        }
        if (len < -1) {
            r.success = false;
            r.error = "negative bulk string length";
            return r;
        }
        const size_t data_begin = end + 2;
        const size_t data_end = data_begin + static_cast<size_t>(len);
        if (data_end + 2 > input.size()) {
            r.success = true;
            r.complete = false;
            return r;
        }
        if (input[data_end] != '\r' || input[data_end + 1] != '\n') {
            r.success = false;
            r.error = "missing CRLF after bulk string body";
            return r;
        }
        r.success = true;
        r.complete = true;
        r.consumed_bytes = data_end + 2;
        r.value = RespValue::bulkString(std::string(input.substr(data_begin, static_cast<size_t>(len))));
        return r;
    }

    if (type == '*') {
        const size_t end = findCRLF(1);
        if (end == std::string_view::npos) {
            r.success = true;
            r.complete = false;
            return r;
        }
        int64_t n = 0;
        if (!parseInt64(input.substr(1, end - 1), &n)) {
            r.success = false;
            r.error = "invalid array length";
            return r;
        }
        if (n < 0) {
            r.success = false;
            r.error = "negative array length is unsupported";
            return r;
        }
        size_t offset = end + 2;
        std::vector<RespValue> elems;
        elems.reserve(static_cast<size_t>(n));
        for (int64_t i = 0; i < n; ++i) {
            RespParseResult sub = parseOne(input.substr(offset));
            if (!sub.success) return sub;
            if (!sub.complete) {
                r.success = true;
                r.complete = false;
                return r;
            }
            if (sub.consumed_bytes == 0) {
                r.success = false;
                r.error = "array element consumed 0 bytes";
                return r;
            }
            offset += sub.consumed_bytes;
            elems.push_back(std::move(sub.value));
        }
        r.success = true;
        r.complete = true;
        r.consumed_bytes = offset;
        r.value = RespValue::arrayValue(std::move(elems));
        return r;
    }

    r.success = false;
    r.error = std::string("unsupported RESP prefix: ") + type;
    return r;
}

} // namespace

RespParseResult parseRespValue(std::string_view input) {
    return parseOne(input);
}

std::string encodeRespArrayCommand(const std::vector<std::string>& args) {
    std::string out;
    out.reserve(64);
    out += "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}

} // namespace sunkv::client
