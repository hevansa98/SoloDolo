// NMEA 0183 parsing: checksum validation and typed sentences. Stateless and independent of any receiver model.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace dolo::nmea
{

    struct Rmc
    { // recommended minimum: time, date, speed and course over ground
        bool valid = false;
        std::optional<uint64_t> timeMs; // Unix ms, only when valid
        std::optional<double> speedKt, trackDeg;
    };

    struct Gga
    {                    // fix data: position, altitude, satellites used
        int quality = 0; // 0 = no fix
        std::optional<double> lat, lon, altMslM, hdop;
        uint32_t satsUsed = 0;
    };

    struct Gsa
    {                    // dilution of precision and fix type; multi-constellation receivers send one per constellation
        int fixType = 0; // 1 none, 2 2D, 3 3D
        std::optional<double> vdop;
    };

    struct Gsv
    {                       // satellites in view, reported per constellation (talker)
        std::string talker; // "GP" GPS, "GL" GLONASS, "GA" Galileo, "GB"/"BD" BeiDou
        uint32_t inView = 0;
    };

    using Sentence = std::variant<Rmc, Gga, Gsa, Gsv>;

    /// Parses one line ("$....*HH", trailing CR/LF allowed). Returns nullopt for a missing or wrong checksum,
    /// malformed fields, or a sentence type Dolo does not use. Any talker prefix is accepted.
    std::optional<Sentence> parse(std::string_view line);

} // namespace dolo::nmea
