#pragma once

#include <list>
#include <string>

struct ListValue {
    std::list<std::string> data;

    ListValue() = default;
    explicit ListValue(const std::list<std::string>& v) : data(v) {}

    void push_front(const std::string& v) { data.push_front(v); }
    void push_back(const std::string& v) { data.push_back(v); }
    void pop_front() { data.pop_front(); }
    void pop_back() { data.pop_back(); }
    std::string& front() { return data.front(); }
    const std::string& front() const { return data.front(); }
    std::string& back() { return data.back(); }
    const std::string& back() const { return data.back(); }
    bool empty() const { return data.empty(); }
    size_t size() const { return data.size(); }

    auto begin() { return data.begin(); }
    auto end() { return data.end(); }
    auto begin() const { return data.begin(); }
    auto end() const { return data.end(); }

    operator const std::list<std::string>&() const { return data; }
    operator std::list<std::string>&() { return data; }
};
