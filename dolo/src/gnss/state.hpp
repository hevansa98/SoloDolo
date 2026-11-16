// What Dolo knows about ownship GNSS right now, independent of which receiver produced it.
#pragma once

#include <cstdint>
#include <optional>

#include "core/ownship.hpp"
#include "solodolo.pb.h"

namespace dolo
{

    struct GnssState
    {
        int mode = 0; // 0 unknown, 1 no fix, 2 2D, 3 3D
        uint64_t timeMs = 0;
        std::optional<double> lat, lon, altM, speedKt, track, climbFpm, eph, hdop, vdop;
        uint32_t used = 0, visible = 0;

        bool hasPosition() const { return mode >= 2 && lat && lon; }
        std::optional<Ownship> ownship() const;
        solodolo::v1::GnssFix toProto() const;
    };

} // namespace dolo
