#include "WalReader.h"

#include <cstdint>
#include <fstream>
#include <vector>
#include <filesystem>

#include "WalCodec.h"

namespace sunkv::storage2 {

WalReader::WalReader(std::string path) : path_(std::move(path)) {}

bool WalReader::readAll(std::vector<MutationBatch>* out) {
    if (!out) return false;
    out->clear();
    last_stats_ = ReadStats{};

    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        return true; // 不存在视为无 WAL
    }

    std::error_code ec;
    const auto fsize = std::filesystem::file_size(path_, ec);
    if (!ec) {
        last_stats_.file_bytes = static_cast<size_t>(fsize);
    }

    // 流式解码：避免把整份 WAL 一次性读入内存。
    constexpr size_t kChunkBytes = 1024 * 1024; // 1MB
    std::vector<uint8_t> pending;
    pending.reserve(kChunkBytes * 2);
    std::vector<uint8_t> chunk(kChunkBytes);
    size_t consumed_prefix = 0; // 已从文件头累计消费的字节数

    while (true) {
        in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const auto got = in.gcount();
        if (got < 0) {
            return false;
        }
        if (got > 0) {
            pending.insert(pending.end(), chunk.begin(), chunk.begin() + got);
        }

        size_t off = 0;
        while (off < pending.size()) {
            Mutation mu;
            size_t probe = off;
            auto st = WalCodec::decodeOneStatus(pending.data(), pending.size(), &probe, &mu);
            if (st == WalCodec::DecodeStatus::Ok) {
                off = probe;
                MutationBatch b;
                b.push_back(std::move(mu));
                out->push_back(std::move(b));
                last_stats_.decoded_mutations += 1;
                continue;
            }

            if (st == WalCodec::DecodeStatus::IncompleteTail) {
                if (in.eof()) {
                    // 文件末尾残缺：容忍并停止回放
                    last_stats_.saw_incomplete_tail = true;
                    last_stats_.stopped_offset = consumed_prefix + off;
                    return true;
                }
                // 不是 EOF，说明需要更多字节继续解码
                break;
            }

            last_stats_.saw_corrupt = true;
            last_stats_.stopped_offset = consumed_prefix + off;
            return false;
        }

        if (off > 0) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(off));
            consumed_prefix += off;
        }

        if (in.bad()) {
            return false;
        }
        if (in.eof()) {
            // 干净 EOF：若仍有未消费字节，也按不完整尾巴容忍
            if (!pending.empty()) {
                last_stats_.saw_incomplete_tail = true;
            }
            last_stats_.stopped_offset = consumed_prefix;
            return true;
        }
    }
}

bool WalReader::readAllMutations(std::vector<Mutation>* out) {
    if (!out) return false;
    std::vector<MutationBatch> batches;
    if (!readAll(&batches)) return false;
    out->clear();
    out->reserve(batches.size());
    for (auto& b : batches) {
        if (b.empty()) continue;
        out->push_back(std::move(b[0]));
    }
    return true;
}

} // namespace sunkv::storage2

