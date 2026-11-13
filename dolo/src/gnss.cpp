#include "gnss.hpp"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

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


/// Verifies "$....*HH" and returns the body between '$' and '*', or nullopt.
std::optional<std::string_view> nmeaBody(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.remove_suffix(1);
    const size_t star = line.rfind('*');
    if (line.size() < 4 || line.front() != '$' || star == std::string_view::npos || star + 3 != line.size()) return std::nullopt;
    const std::string_view body = line.substr(1, star - 1);
    unsigned sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char* end = nullptr;
    const std::string hex(line.substr(star + 1));
    const unsigned long want = std::strtoul(hex.c_str(), &end, 16);
    if (*end != '\0' || want != sum) return std::nullopt;
    return body;
}

std::vector<std::string_view> splitFields(std::string_view body) {
    std::vector<std::string_view> out;
    for (size_t pos = 0;;) {
        const size_t comma = body.find(',', pos);
        out.push_back(body.substr(pos, comma == std::string_view::npos ? comma : comma - pos));
        if (comma == std::string_view::npos) return out;
        pos = comma + 1;
    }
}

std::optional<double> nmeaNum(const std::vector<std::string_view>& f, size_t i) {
    if (i >= f.size() || f[i].empty()) return std::nullopt;
    const std::string s(f[i]);
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (*end != '\0' || !std::isfinite(v)) return std::nullopt;
    return v;
}

/// Receivers report 99.99 for "unknown" dilution of precision.
std::optional<double> dop(std::optional<double> v) { return v && *v < 99.0 ? v : std::nullopt; }

/// "ddmm.mmmm" + hemisphere -> signed degrees. degDigits is 2 for latitude, 3 for longitude.
std::optional<double> nmeaCoord(const std::vector<std::string_view>& f, size_t i, int degDigits) {
    const auto raw = nmeaNum(f, i);
    if (!raw || i + 1 >= f.size() || f[i + 1].size() != 1) return std::nullopt;
    const double deg = std::floor(*raw / 100.0), min = *raw - deg * 100.0;
    if (min >= 60.0 || deg >= (degDigits == 2 ? 91 : 181)) return std::nullopt;
    double v = deg + min / 60.0;
    const char h = f[i + 1][0];
    if (h == 'S' || h == 'W') v = -v;
    else if (h != 'N' && h != 'E') return std::nullopt;
    return v;
}

/// "hhmmss.ss" + "ddmmyy" -> Unix ms.
std::optional<uint64_t> nmeaTimeMs(std::string_view hms, std::string_view dmy) {
    if (hms.size() < 6 || dmy.size() != 6) return std::nullopt;
    for (size_t i = 0; i < 6; ++i)
        if (hms[i] < '0' || hms[i] > '9' || dmy[i] < '0' || dmy[i] > '9') return std::nullopt;
    auto two = [](std::string_view s, size_t at) { return (s[at] - '0') * 10 + (s[at + 1] - '0'); };
    std::tm tm{};
    tm.tm_hour = two(hms, 0);
    tm.tm_min = two(hms, 2);
    tm.tm_sec = two(hms, 4);
    tm.tm_mday = two(dmy, 0);
    tm.tm_mon = two(dmy, 2) - 1;
    tm.tm_year = 100 + two(dmy, 4);  // GPS dates are 2000-2099
    if (tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 60 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_mon < 0 || tm.tm_mon > 11)
        return std::nullopt;
    uint64_t ms = static_cast<uint64_t>(timegm(&tm)) * 1000;
    if (hms.size() > 7 && hms[6] == '.') {
        int digits = 0, frac = 0;
        for (size_t i = 7; i < hms.size() && hms[i] >= '0' && hms[i] <= '9' && digits < 3; ++i, ++digits) frac = frac * 10 + (hms[i] - '0');
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

bool GnssState::applyNmea(std::string_view line) {
    const auto body = nmeaBody(line);
    if (!body || body->size() < 6 || (*body)[5] != ',') return false;
    const std::string_view talker = body->substr(0, 2), type = body->substr(2, 3);
    const auto f = splitFields(*body);

    if (type == "RMC") {
        // $--RMC,time,status,lat,N,lon,E,speed(kt),course,date,...
        if (f.size() < 10) return false;
        const bool valid = f[2] == "A";
        if (valid) {
            if (const auto ms = nmeaTimeMs(f[1], f[9])) timeMs = *ms;
        }
        speedKt = valid ? nmeaNum(f, 7) : std::nullopt;
        track = valid ? nmeaNum(f, 8) : std::nullopt;  // empty while stationary
        return false;
    }
    if (type == "GGA") {
        // $--GGA,time,lat,N,lon,E,quality,sats used,hdop,alt MSL,M,...
        if (f.size() < 10) return false;
        const int quality = static_cast<int>(nmeaNum(f, 6).value_or(0));
        const auto la = nmeaCoord(f, 2, 2), lo = nmeaCoord(f, 4, 3);
        used = static_cast<uint32_t>(nmeaNum(f, 7).value_or(0));
        hdop = dop(nmeaNum(f, 8));
        if (quality > 0 && la && lo) {
            lat = la;
            lon = lo;
            altM = nmeaNum(f, 9);
            mode = gsaMode >= 2 ? gsaMode : altM ? 3 : 2;
        } else {
            mode = 1;
            lat = lon = altM = std::nullopt;
            speedKt = track = climbFpm = std::nullopt;
        }
        return true;
    }
    if (type == "GSA") {
        // $--GSA,A,fixtype(1/2/3),sat1..sat12,pdop,hdop,vdop
        if (f.size() < 18) return false;
        gsaMode = static_cast<int>(nmeaNum(f, 2).value_or(0));
        vdop = dop(nmeaNum(f, 17));
        return false;
    }
    if (type == "GSV") {
        // $--GSV,total msgs,msg #,sats in view,...  Each constellation reports under its own talker.
        if (f.size() < 4) return false;
        if (const auto n = nmeaNum(f, 3)) viewByTalker[std::string(talker)] = static_cast<uint32_t>(*n);
        visible = 0;
        for (const auto& [t, n] : viewByTalker) visible += n;
        return false;
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
