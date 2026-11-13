// Length-prefixed framing: uint32 big-endian length, then an Envelope.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace dolo {

constexpr size_t kMaxFrameBytes = 1u << 20;  // 1 MiB

inline std::string encodeFrame(const std::string& payload) {
    if (payload.size() > kMaxFrameBytes) throw std::length_error("frame too large");
    const auto n = static_cast<uint32_t>(payload.size());
    std::string out;
    out.reserve(4 + payload.size());
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>((n >> shift) & 0xff));
    return out += payload;
}

enum class FrameStatus { NeedMore, Frame, Bad };

/// Removes one complete frame from the front of `buf` into `payload`.
/// `Bad` means the advertised length exceeds the limit; the connection should be dropped.
inline FrameStatus extractFrame(std::string& buf, std::string& payload) {
    if (buf.size() < 4) return FrameStatus::NeedMore;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n = (n << 8) | static_cast<uint8_t>(buf[i]);
    if (n > kMaxFrameBytes) return FrameStatus::Bad;
    if (buf.size() < 4 + static_cast<size_t>(n)) return FrameStatus::NeedMore;
    payload.assign(buf, 4, n);
    buf.erase(0, 4 + static_cast<size_t>(n));
    return FrameStatus::Frame;
}

}  // namespace dolo
