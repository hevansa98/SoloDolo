#pragma once

#include <chrono>
#include <cstdint>

namespace dolo {

/// Monotonic seconds, for ages and timers.
inline double monotonicS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline uint64_t unixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace dolo
