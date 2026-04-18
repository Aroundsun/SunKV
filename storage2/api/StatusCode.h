#pragma once

namespace sunkv::storage2 {

enum class StatusCode {
    Ok = 0,
    NotFound = 1,
    WrongType = 2,
    InvalidArg = 3,
    InternalError = 4,
    NotImplemented = 5,
};

} // namespace sunkv::storage2

