#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "StatusCode.h"
#include "../engine/Mutation.h"

namespace sunkv::storage2 {

using MutationBatch = std::vector<Mutation>;

template <class T>
struct StorageResult {
    StatusCode status{StatusCode::Ok};
    T value{};
    MutationBatch mutations{};

    static StorageResult<T> ok(T v, MutationBatch m = {}) {
        StorageResult<T> r;
        r.status = StatusCode::Ok;
        r.value = std::move(v);
        r.mutations = std::move(m);
        return r;
    }

    static StorageResult<T> err(StatusCode s) {
        StorageResult<T> r;
        r.status = s;
        return r;
    }
};

} // namespace sunkv::storage2

