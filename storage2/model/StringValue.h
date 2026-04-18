#pragma once

#include <string>

struct StringValue {
    std::string data;

    StringValue() = default;
    StringValue(const std::string& v) : data(v) {}
    StringValue(std::string&& v) : data(std::move(v)) {}

    StringValue& operator=(const std::string& v) { data = v; return *this; }
    StringValue& operator=(std::string&& v) { data = std::move(v); return *this; }

    void clear() { data.clear(); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }

    const std::string& str() const { return data; }
    std::string& str() { return data; }

    operator const std::string&() const { return data; }
    operator std::string&() { return data; }

    bool operator==(const StringValue& other) const { return data == other.data; }
    bool operator!=(const StringValue& other) const { return data != other.data; }
};
