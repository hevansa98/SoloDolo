#include "gnss.hpp"

#include <cmath>
#include <ctime>

namespace dolo {
namespace {

constexpr double kMsToKt = 1.943844;
constexpr double kMToFt = 3.28084;

std::optional<double> num(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
}

/// "2026-01-01T00:00:00.500Z" -> Unix ms.
std::optional<uint64_t> parseIsoMs(const std::string& s) {
    std::tm tm{};
    const char* rest = strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    if (!rest) return std::nullopt;
    uint64_t ms = static_cast<uint64_t>(timegm(&tm)) * 1000;
    if (*rest == '.') {
        int digits = 0, frac = 0;
        for (++rest; *rest >= '0' && *rest <= '9' && digits < 3; ++rest, ++digits) frac = frac * 10 + (*rest - '0');
        while (digits++ < 3) frac *= 10;
        ms += frac;
    }
    return ms;
}

}  // namespace

bool GnssState::apply(const nlohmann::json& msg) {
    if (!msg.is_object()) return false;
    const std::string cls = msg.value("class", "");
    if (cls == "TPV") {
        mode = msg.value("mode", 0);
        if (const auto t = msg.find("time"); t != msg.end() && t->is_string()) {
            if (const auto ms = parseIsoMs(t->get<std::string>())) timeMs = *ms;
        }
        const bool fix = mode >= 2;
        lat = fix ? num(msg, "lat") : std::nullopt;
        lon = fix ? num(msg, "lon") : std::nullopt;
        altM = mode >= 3 ? (num(msg, "altMSL") ? num(msg, "altMSL") : num(msg, "alt")) : std::nullopt;
        speedKt = num(msg, "speed"); if (speedKt) *speedKt *= kMsToKt;
        track = num(msg, "track");
        climbFpm = num(msg, "climb"); if (climbFpm) *climbFpm *= kMToFt * 60;
        eph = num(msg, "eph");
        return true;
    }
    if (cls == "SKY") {
        if (const auto s = msg.find("satellites"); s != msg.end() && s->is_array()) {
            visible = static_cast<uint32_t>(s->size());
            used = 0;
            for (const auto& sat : *s) used += sat.value("used", false) ? 1 : 0;
        } else if (const auto u = num(msg, "uSat")) {
            used = static_cast<uint32_t>(*u);
        }
        if (const auto n = num(msg, "nSat")) visible = static_cast<uint32_t>(*n);
        if (const auto h = num(msg, "hdop")) hdop = h;
        if (const auto v = num(msg, "vdop")) vdop = v;
    }
    return false;
}

std::optional<Ownship> GnssState::ownship() const {
    if (!hasPosition()) return std::nullopt;
    return Ownship{*lat, *lon, altM ? std::optional<double>(*altM * kMToFt) : std::nullopt};
}

solodolo::v1::GnssFix GnssState::toProto() const {
    solodolo::v1::GnssFix f;
    f.set_fix_mode(mode == 2 ? solodolo::v1::FIX_MODE_2D : mode >= 3 ? solodolo::v1::FIX_MODE_3D : solodolo::v1::FIX_MODE_NO_FIX);
    f.set_fix_unix_ms(timeMs);
    f.set_satellites_used(used);
    f.set_satellites_visible(visible);
    if (hasPosition()) {
        f.set_latitude_deg(*lat);
        f.set_longitude_deg(*lon);
    }
    if (mode >= 3 && altM) f.set_altitude_msl_m(static_cast<float>(*altM));
    if (speedKt) f.set_ground_speed_kt(static_cast<float>(*speedKt));
    if (track) f.set_track_true_deg(static_cast<float>(*track));
    if (climbFpm) f.set_climb_fpm(static_cast<float>(*climbFpm));
    if (hdop) f.set_hdop(static_cast<float>(*hdop));
    if (vdop) f.set_vdop(static_cast<float>(*vdop));
    if (eph) f.set_horizontal_error_m(static_cast<float>(*eph));
    return f;
}

}  // namespace dolo
