// u-blox UBX binary protocol: framing, checksum, and the handful of messages Dolo uses. Stateless and independent
// of any receiver model; which messages to enable and how to merge them is the receiver class's business.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dolo::ubx
{

    constexpr uint8_t kSync1 = 0xB5, kSync2 = 0x62;
    constexpr size_t kHeaderBytes = 6, kOverheadBytes = 8; // sync(2) class id length(2) ... checksum(2)
    constexpr size_t kMaxPayloadBytes = 1024;              // larger than any message Dolo reads; bounds resync

    // Message classes and ids.
    constexpr uint8_t kClassNav = 0x01, kNavPvt = 0x07, kNavDop = 0x04, kNavSat = 0x35;
    constexpr uint8_t kClassAck = 0x05, kAckNak = 0x00, kAckAck = 0x01;
    constexpr uint8_t kClassCfg = 0x06, kCfgMsg = 0x01;
    // Standard NMEA message ids, used with UBX-CFG-MSG.
    constexpr uint8_t kClassNmea = 0xF0, kNmeaGga = 0x00, kNmeaGll = 0x01, kNmeaGsa = 0x02, kNmeaGsv = 0x03,
                      kNmeaRmc = 0x04, kNmeaVtg = 0x05;

    struct Frame
    {
        uint8_t cls = 0, id = 0;
        std::string payload;
    };

    /// Builds a complete frame (sync, header, payload, checksum).
    std::string encode(uint8_t cls, uint8_t id, std::string_view payload = {});

    /// UBX-CFG-MSG: output message `cls`/`id` `rate` times per navigation epoch on the port the command arrives on
    /// (0 turns it off). Takes effect immediately and is not saved to the module's flash.
    std::string cfgMsg(uint8_t cls, uint8_t id, uint8_t rate);

    /// Fletcher checksum over class, id, length and payload, as {ck_a, ck_b}.
    std::pair<uint8_t, uint8_t> checksum(uint8_t cls, uint8_t id, std::string_view payload);

    /// UBX-NAV-PVT (92 bytes): position, velocity and time solution, in SI units.
    struct NavPvt
    {
        uint32_t iTow = 0;              // GPS time of week, ms; identifies the epoch
        std::optional<uint64_t> timeMs; // Unix ms, only when the module flags date and time valid
        int fixType = 0;                // 0 none, 1 dead reckoning, 2 2D, 3 3D, 4 GNSS + dead reckoning, 5 time only
        bool fixOk = false;             // within the receiver's DOP and accuracy masks
        uint32_t satsUsed = 0;
        double latDeg = 0, lonDeg = 0;
        double hMslM = 0;            // height above mean sea level
        double hAccM = 0, vAccM = 0; // estimated accuracy
        double groundSpeedMs = 0;
        double headingDeg = 0; // course over ground; meaningless while stationary
        double velDownMs = 0;
    };
    std::optional<NavPvt> decodeNavPvt(const Frame &f);

    /// UBX-NAV-DOP (18 bytes): dilution of precision.
    struct NavDop
    {
        uint32_t iTow = 0;
        double hdop = 0, vdop = 0;
    };
    std::optional<NavDop> decodeNavDop(const Frame &f);

    /// UBX-NAV-SAT: every satellite the module tracks (all constellations, including SBAS).
    struct NavSat
    {
        uint32_t iTow = 0;
        uint32_t visible = 0, used = 0;
    };
    std::optional<NavSat> decodeNavSat(const Frame &f);

} // namespace dolo::ubx
