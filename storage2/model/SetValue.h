#pragma once

#include <set>
#include <string>

struct SetValue {
    std::set<std::string> data;

    SetValue() = default;
    explicit SetValue(const std::set<std::string>& v) : data(v) {}

    auto insert(const std::string& v) { return data.insert(v); }
    size_t erase(const std::string& v) { return data.erase(v); }
    size_t count(const std::string& v) const { return data.count(v); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }

    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }

    operator const std::set<std::string>&() const { return data; }
    operator std::set<std::string>&() { return data; }
};
