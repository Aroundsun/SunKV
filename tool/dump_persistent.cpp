// 独立小工具：读取 storage2 快照 / WAL，写出纯文本（不参与运行时逻辑）。
// 用法（在仓库根目录执行编译产物）：
//   ./build/dump_persistent snapshot <snapshot.bin>
//   ./build/dump_persistent wal <wal基础路径>
// 输出：<源码>/tool/<输入文件名>_snapshot.txt 或 _wal.txt
// 说明：文本导出体积通常明显大于二进制；完成后会在 stderr 打一行
//   wal_chain_input_bytes / out_txt_bytes / ratio_txt_per_binary（便于对比，并非 WAL 被改写）。
// 输出 .txt 一律 trunc：先清空再写，避免本次内容更短时尾部残留旧数据。

#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>

#include <filesystem>

#include "storage2/engine/Mutation.h"
#include "storage2/persistence/SnapshotReader.h"
#include "storage2/persistence/WalReader.h"

namespace fs = std::filesystem;

/// 覆盖写文本：显式 trunc，保证清空原文件再写入。
static constexpr std::ios::openmode kDumpTxtTrunc = std::ios::out | std::ios::trunc;

#ifndef TOOL_OUT_DIR_STR
#define TOOL_OUT_DIR_STR "./tool"
#endif

static std::string esc_line(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '\n') {
            o += "\\n";
        } else if (c == '\r') {
            o += "\\r";
        } else if (c == '\t') {
            o += "\\t";
        } else if (c < 32 || c == 127) {
            std::ostringstream hex;
            hex << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            o += hex.str();
        } else {
            o += static_cast<char>(c);
        }
    }
    return o;
}

static const char* data_type_name(DataType t) {
    switch (t) {
        case DataType::STRING:
            return "STRING";
        case DataType::LIST:
            return "LIST";
        case DataType::SET:
            return "SET";
        case DataType::HASH:
            return "HASH";
        default:
            return "?";
    }
}

static void dump_datavalue(std::ostream& os, const DataValue& v, const std::string& indent) {
    os << indent << "type=" << data_type_name(v.type) << '\n';
    os << indent << "ttl_seconds=" << v.ttl_seconds << '\n';
    switch (v.type) {
        case DataType::STRING:
            os << indent << "string=" << esc_line(v.string_value.str()) << '\n';
            break;
        case DataType::LIST: {
            size_t i = 0;
            for (const auto& e : v.list_value.data) {
                os << indent << "list[" << i << "]=" << esc_line(e) << '\n';
                ++i;
            }
            break;
        }
        case DataType::SET: {
            size_t i = 0;
            for (const auto& e : v.set_value.data) {
                os << indent << "set[" << i << "]=" << esc_line(e) << '\n';
                ++i;
            }
            break;
        }
        case DataType::HASH:
            for (const auto& kv : v.hash_value.data) {
                os << indent << "hash[" << esc_line(kv.first) << "]=" << esc_line(kv.second) << '\n';
            }
            break;
    }
}

static void dump_record(std::ostream& os, const sunkv::storage2::Record& r, const std::string& indent) {
    os << indent << "expire_at_us=" << r.expire_at_us << '\n';
    os << indent << "version=" << r.version << '\n';
    dump_datavalue(os, r.value, indent);
}

static std::string mutation_type_str(sunkv::storage2::MutationType t) {
    switch (t) {
        case sunkv::storage2::MutationType::PutRecord:
            return "PutRecord";
        case sunkv::storage2::MutationType::DelKey:
            return "DelKey";
        case sunkv::storage2::MutationType::ClearAll:
            return "ClearAll";
        default:
            return "?";
    }
}

static bool dump_snapshot(const std::string& path, const std::string& out_path) {
    std::vector<std::pair<std::string, sunkv::storage2::Record>> rows;
    if (!sunkv::storage2::SnapshotReader::readFromFile(path, &rows)) {
        std::cerr << "snapshot read failed: " << path << '\n';
        return false;
    }
    std::ofstream out(out_path, kDumpTxtTrunc);
    if (!out) {
        std::cerr << "open output failed: " << out_path << '\n';
        return false;
    }
    out << "file=" << path << '\n';
    out << "magic=S2 version=1\n";
    out << "records=" << rows.size() << '\n';
    out << "---\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        out << '[' << i << "] key=" << esc_line(rows[i].first) << '\n';
        dump_record(out, rows[i].second, "  ");
        out << "---\n";
    }
    out.flush();
    out.close();
    std::uintmax_t in_bytes = 0;
    std::uintmax_t out_bytes = 0;
    {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            in_bytes = fs::file_size(path, ec);
        }
        if (fs::exists(out_path, ec)) {
            out_bytes = fs::file_size(out_path, ec);
        }
    }
    std::cerr << "dump_persistent snapshot: input_bytes=" << in_bytes << " out_txt_bytes=" << out_bytes
              << " records=" << rows.size();
    if (in_bytes > 0) {
        std::cerr << " ratio_txt_per_binary=" << std::fixed << std::setprecision(2)
                  << (static_cast<double>(out_bytes) / static_cast<double>(in_bytes));
    }
    std::cerr << '\n';

    std::cout << out_path << '\n';
    return true;
}

static bool dump_wal(const std::string& path, const std::string& out_path) {
    std::vector<sunkv::storage2::Mutation> muts;
    if (!sunkv::storage2::WalReader::readAllMutationsWalChain(path, &muts)) {
        std::cerr << "wal read failed: " << path << '\n';
        return false;
    }
    std::ofstream out(out_path, kDumpTxtTrunc);
    if (!out) {
        std::cerr << "open output failed: " << out_path << '\n';
        return false;
    }
    out << "base=" << path << '\n';
    out << "mutations=" << muts.size() << '\n';
    out << "---\n";
    for (size_t i = 0; i < muts.size(); ++i) {
        const auto& m = muts[i];
        out << '[' << i << "] ts_us=" << m.ts_us << " type=" << mutation_type_str(m.type)
            << " key=" << esc_line(m.key) << '\n';
        if (m.type == sunkv::storage2::MutationType::PutRecord && m.record.has_value()) {
            dump_record(out, *m.record, "  ");
        }
        out << "---\n";
    }
    out.flush();
    out.close();
    std::uintmax_t out_bytes = 0;
    {
        std::error_code ec;
        if (fs::exists(out_path, ec)) {
            out_bytes = fs::file_size(out_path, ec);
        }
    }
    const std::uintmax_t in_bytes = sunkv::storage2::WalReader::walChainFileBytesTotal(path);
    std::cerr << "dump_persistent wal: wal_chain_input_bytes=" << in_bytes << " out_txt_bytes=" << out_bytes
              << " mutations=" << muts.size();
    if (in_bytes > 0) {
        std::cerr << " ratio_txt_per_binary=" << std::fixed << std::setprecision(2)
                  << (static_cast<double>(out_bytes) / static_cast<double>(in_bytes));
    }
    std::cerr << '\n';

    std::cout << out_path << '\n';
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage:\n  " << (argc > 0 ? argv[0] : "dump_persistent")
                  << " snapshot <file>\n  " << (argc > 0 ? argv[0] : "dump_persistent") << " wal <file>\n";
        return 2;
    }
    std::string mode = argv[1];
    std::string infile = argv[2];
    fs::path inp(infile);
    std::string base = inp.filename().string();
    if (base.empty()) {
        base = "dump";
    }

    fs::path out_dir(TOOL_OUT_DIR_STR);
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    if (mode == "snapshot") {
        std::string outp = (out_dir / (base + "_snapshot.txt")).string();
        return dump_snapshot(infile, outp) ? 0 : 1;
    }
    if (mode == "wal") {
        std::string outp = (out_dir / (base + "_wal.txt")).string();
        return dump_wal(infile, outp) ? 0 : 1;
    }
    std::cerr << "unknown mode: " << mode << " (use snapshot or wal)\n";
    return 2;
}
