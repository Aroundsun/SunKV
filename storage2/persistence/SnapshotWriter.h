#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../backend/IBackend.h"

namespace sunkv::storage2 {

class SnapshotWriter final {
public:
    static constexpr uint8_t kVersion = 1;
    static bool writeToFile(IBackend& backend, const std::string& path);
    static bool writeToFile(const std::vector<std::pair<std::string, Record>>& records, const std::string& path);
};

} // namespace sunkv::storage2

