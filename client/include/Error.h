#pragma once

#include <string>

namespace sunkv::client {

enum class ErrorCode {
    Ok = 0,
    NotConnected,
    ConnectFailed,
    Timeout,
    IoError,
    ProtocolError,
    ServerError,
    InvalidResponse,
    InvalidArgument,
};

struct Error {
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

} // namespace sunkv::client
