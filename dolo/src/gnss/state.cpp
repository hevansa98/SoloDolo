#include "gnss/state.hpp"

#include "gnss/units.hpp"

namespace dolo
{

    std::optional<Ownship> GnssState::ownship() const
    {
        if (!hasPosition())
            return std::nullopt;
        return Ownship{*lat, *lon, altM ? std::optional<double>(*altM * kMToFt) : std::nullopt};
    }

    solodolo::v1::GnssFix GnssState::toProto() const
    {
        solodolo::v1::GnssFix f;
        f.set_fix_mode(mode == 2 ? solodolo::v1::FIX_MODE_2D : mode >= 3 ? solodolo::v1::FIX_MODE_3D
                                                                         : solodolo::v1::FIX_MODE_NO_FIX);
        f.set_fix_unix_ms(timeMs);
        f.set_satellites_used(used);
        f.set_satellites_visible(visible);
        if (hasPosition())
        {
            f.set_latitude_deg(*lat);
            f.set_longitude_deg(*lon);
        }
        if (mode >= 3 && altM)
            f.set_altitude_msl_m(static_cast<float>(*altM));
        if (speedKt)
            f.set_ground_speed_kt(static_cast<float>(*speedKt));
        if (track)
            f.set_track_true_deg(static_cast<float>(*track));
        if (climbFpm)
            f.set_climb_fpm(static_cast<float>(*climbFpm));
        if (hdop)
            f.set_hdop(static_cast<float>(*hdop));
        if (vdop)
            f.set_vdop(static_cast<float>(*vdop));
        if (eph)
            f.set_horizontal_error_m(static_cast<float>(*eph));
        return f;
    }

} // namespace dolo
