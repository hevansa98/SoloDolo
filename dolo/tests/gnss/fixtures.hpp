// Shared helpers and captured data for the GNSS tests.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "gnss/ubx.hpp"
#include "support.hpp"

inline std::string sentence(const std::string& body) {
    unsigned sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return "$" + body + tail;
}

// Frames captured from the MAX-M8Q HAT (GPS time of week 15554000, stationary, 3D fix with 16 satellites).
inline const std::string kRealNavPvt(
    "\xb5\x62\x01\x07\x5c\x00\xd0\x55\xed\x00\xea\x07\x0b\x0f\x04\x12\x38\x37\x3a\x00\x00\x00\x96\x48\x07\x00\x03\x03\x0a\x10"
    "\xeb\xbd\x30\xc6\xfe\x3c\xb2\x13\xe7\xd0\x01\x00\x43\x33\x02\x00\x80\x09\x00\x00\xeb\x11\x00\x00\x18\x00\x00\x00\xf3\xff"
    "\xff\xff\x5a\x00\x00\x00\x1b\x00\x00\x00\x00\x00\x00\x00\xac\x01\x00\x00\x0c\xe9\x08\x01\x93\x00\x00\x00\xe0\x4a\x23\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x9f\x42",
    100);
inline const std::string kRealNavDop(
    "\xb5\x62\x01\x04\x12\x00\xd0\x55\xed\x00\xab\x00\x93\x00\x57\x00\x82\x00\x46\x00\x33\x00\x30\x00\xe9\x6b", 26);

inline void putU32(std::string &s, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

/// The same frame for a different epoch: patches iTOW and re-encodes so the checksum stays valid.
inline std::string atItow(const std::string &frame, uint32_t iTow)
{
    std::string payload = frame.substr(6, frame.size() - 8);
    putU32(payload, 0, iTow);
    return ubx::encode(static_cast<uint8_t>(frame[2]), static_cast<uint8_t>(frame[3]), payload);
}

/// UBX-NAV-SAT with `n` satellites, the first `used` of them flagged as used in the solution.
inline std::string navSat(uint32_t iTow, int n, int used)
{
    std::string payload(8 + 12 * n, '\0');
    putU32(payload, 0, iTow);
    payload[4] = 1;  // version
    payload[5] = static_cast<char>(n);
    for (int i = 0; i < n; ++i) payload[8 + 12 * i + 8] = i < used ? '\x0f' : '\x07';  // bit 3 is svUsed
    return ubx::encode(ubx::kClassNav, ubx::kNavSat, payload);
}
