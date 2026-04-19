#pragma once

namespace sunkv::storage2 {

enum class StatusCode {
    Ok = 0,
    NotFound = 1,
    WrongType = 2,
    InvalidArg = 3,
    InternalError = 4,
    NotImplemented = 5,
    /// 存储估算用量超过配置上限（近似统计，仅作保护阈值）
    QuotaExceeded = 6,
};

} // namespace sunkv::storage2

