#pragma once

#include <cstdint>
#include <vector>

#include "../api/StorageResult.h"

namespace sunkv::storage2 {

class WalCodec {
public:
    static constexpr uint8_t kVersion = 1;

    enum class DecodeStatus {
        Ok = 0,
        IncompleteTail = 1, // 数据到达文件末尾但不足以组成一条完整记录（崩溃尾巴）
        Corrupt = 2,        // magic/version 不匹配等真实损坏
    };

    static void appendMutation(std::vector<uint8_t>& out, const Mutation& m);
    static std::vector<uint8_t> encodeBatch(const MutationBatch& batch);

    // 解码：从 bytes[off:] 读取一条 mutation，成功则推进 off 并写入 out。
    static bool decodeOne(const uint8_t* data, size_t len, size_t* off, Mutation* out);
    static DecodeStatus decodeOneStatus(const uint8_t* data, size_t len, size_t* off, Mutation* out);
};

} // namespace sunkv::storage2

