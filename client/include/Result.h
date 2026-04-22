#pragma once

#include <string>
#include <utility>

#include "Error.h"

namespace sunkv::client {

template <typename T>
struct Result {
    bool ok{false};
    T value{};
    Error error{};

    static Result<T> success(T v) {
        Result<T> r;
        r.ok = true;
        r.value = std::move(v);
        r.error = Error{};
        return r;
    }

    static Result<T> failure(ErrorCode code, const std::string& message) {
        Result<T> r;
        r.ok = false;
        r.error = Error{code, message};
        return r;
    }
};

template <>
struct Result<void> {
    bool ok{false};
    Error error{};

    static Result<void> success() {
        Result<void> r;
        r.ok = true;
        r.error = Error{};
        return r;
    }

    static Result<void> failure(ErrorCode code, const std::string& message) {
        Result<void> r;
        r.ok = false;
        r.error = Error{code, message};
        return r;
    }
};

} // namespace sunkv::client
