#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../engine/Mutation.h"

namespace sunkv::storage2 {

// 只负责 Record 的 CRUD，不负责命令语义
class IBackend {
public:
    virtual ~IBackend() = default;

    // 获取记录
    virtual std::optional<Record> getRecord(const std::string& key) = 0;
    // 设置记录；失败（例如超过存储配额）返回 false
    virtual bool putRecord(const std::string& key, const Record& record) = 0;
    // 删除键
    virtual bool delKey(const std::string& key) = 0;
    // 清空所有
    virtual void clearAll() = 0;
    // 获取大小
    virtual int64_t size() = 0;
    // 获取所有键
    virtual std::vector<std::string> keys() = 0;

    // 恢复/回放路径可跳过存储配额检查（正常命令路径应关闭）
    virtual void setBypassStorageLimit(bool on) { (void)on; }
};

} // namespace sunkv::storage2

