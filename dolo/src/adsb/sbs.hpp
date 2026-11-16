// dump1090 SBS (BaseStation, port 30003) line parser.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dolo {

/// Fields carried by one message; only the ones present are merged into the track.
struct TrackFields {
    std::optional<std::string> callsign, squawk;
    std::optional<double> lat, lon, groundSpeedKt, trackDeg;
    std::optional<int> altBaroFt, altGeomFt, vrateFpm;
    std::optional<bool> onGround;
};

struct SbsMessage {
    uint32_t icao = 0;
    TrackFields fields;
};

/// Returns nullopt for anything that isn't a usable MSG line.
std::optional<SbsMessage> parseSbsLine(std::string_view line);

}  // namespace dolo
