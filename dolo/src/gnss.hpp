// gpsd JSON (TPV + SKY) state. Optional: only used when gnss.enabled is true.
#pragma once

#include <cstdint>
#include <optional>

#include <nlohmann/json.hpp>

#include "aircraft.hpp"
#include "solodolo.pb.h"

namespace dolo {

struct GnssState {
    int mode = 0;
    uint64_t timeMs = 0;
    std::optional<double> lat, lon, altM, speedKt, track, climbFpm, eph, hdop, vdop;
    uint32_t used = 0, visible = 0;

    /// Applies one gpsd message. Returns true for a TPV (position) report.
    bool apply(const nlohmann::json& msg);
    bool hasPosition() const { return mode >= 2 && lat && lon; }
    std::optional<Ownship> ownship() const;
    solodolo::v1::GnssFix toProto() const;
};

}  // namespace dolo
