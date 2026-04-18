#pragma once

#include <map>
#include <string>

struct HashValue {
    std::map<std::string, std::string> data;

    HashValue() = default;
    explicit HashValue(const std::map<std::string, std::string>& v) : data(v) {}

    auto find(const std::string& k) { return data.find(k); }
    auto find(const std::string& k) const { return data.find(k); }
    auto end() { return data.end(); }
    auto end() const { return data.end(); }
    std::string& operator[](const std::string& k) { return data[k]; }
    size_t erase(const std::string& k) { return data.erase(k); }
    size_t count(const std::string& k) const { return data.count(k); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }
    void emplace(std::string&& k, std::string&& v) { data.emplace(std::move(k), std::move(v)); }

    auto begin() { return data.begin(); }
    auto endIter() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto endIter() const { return data.end(); }

    operator const std::map<std::string, std::string>&() const { return data; }
    operator std::map<std::string, std::string>&() { return data; }
};
