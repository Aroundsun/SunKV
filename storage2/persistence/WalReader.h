#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../api/StorageResult.h"

namespace sunkv::storage2 {

class WalReader final {
public:
    static constexpr uint8_t kVersion = 1;

    explicit WalReader(std::string path);

    // 读取所有条目并返回“按条目 batch”（当前 writer 是逐条写入）。
    bool readAll(std::vector<MutationBatch>* out);
    bool readAllMutations(std::vector<Mutation>* out);

    /// 按时间顺序回放主 WAL 及其滚动归档（`<path>.<N>`），用于恢复。
    static bool readAllMutationsWalChain(const std::string& primary_path, std::vector<Mutation>* out);

    struct ReadStats {
        size_t file_bytes{0};
        size_t decoded_mutations{0};
        size_t stopped_offset{0};
        bool saw_incomplete_tail{false};
        bool saw_corrupt{false};
    };
    const ReadStats& lastReadStats() const { return last_stats_; }

private:
    std::string path_;
    ReadStats last_stats_{};
};

} // namespace sunkv::storage2

