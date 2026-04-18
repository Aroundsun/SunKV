#include "Time.h"

#include <chrono>

namespace sunkv::storage2 {

int64_t nowEpochUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace sunkv::storage2

