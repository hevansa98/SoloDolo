// GNSS state from gpsd JSON (TPV + SKY) or raw NMEA 0183. Optional: only used when gnss.enabled is true.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "aircraft.hpp"
#include "solodolo.pb.h"

namespace dolo {

struct GnssState {
    int mode = 0;
    uint64_t timeMs = 0;
    std::optional<double> lat, lon, altM, speedKt, track, climbFpm, eph, hdop, vdop;
    uint32_t used = 0, visible = 0;
    int gsaMode = 0;                              // NMEA GSA fix type, 0 until one is seen
    std::map<std::string, uint32_t> viewByTalker;  // NMEA GSV satellites in view per constellation

    /// Applies one gpsd message. Returns true for a TPV (position) report.
    bool apply(const nlohmann::json& msg);
    /// Applies one NMEA sentence (RMC, GGA, GSA, GSV from any talker). Returns true for a GGA (position) report.
    /// Sentences with a missing or wrong checksum are ignored.
    bool applyNmea(std::string_view line);
    bool hasPosition() const { return mode >= 2 && lat && lon; }
    std::optional<Ownship> ownship() const;
    solodolo::v1::GnssFix toProto() const;
};

}  // namespace dolo
