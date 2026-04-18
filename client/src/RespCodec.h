#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "client/include/RespValue.h"

namespace sunkv::client {

struct RespParseResult {
    bool success{false};
    bool complete{false};
    size_t consumed_bytes{0};
    RespValue value{};
    std::string error;
};

RespParseResult parseRespValue(std::string_view input);
std::string encodeRespArrayCommand(const std::vector<std::string>& args);

} // namespace sunkv::client
