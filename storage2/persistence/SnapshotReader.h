#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../backend/IBackend.h"

namespace sunkv::storage2 {

class SnapshotReader final {
public:
    static constexpr uint8_t kVersion = 1;
    static bool loadFromFile(IBackend& backend, const std::string& path);
    static bool readFromFile(const std::string& path, std::vector<std::pair<std::string, Record>>* out);
};

} // namespace sunkv::storage2

