#include "sbs.hpp"

#include <cstdlib>
#include <vector>

namespace dolo {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

std::optional<double> number(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    const std::string tmp(s);
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    return *end == '\0' ? std::optional<double>(v) : std::nullopt;
}

std::optional<int> integer(std::string_view s) {
    const auto v = number(s);
    return v ? std::optional<int>(static_cast<int>(*v)) : std::nullopt;
}

std::optional<std::string> text(std::string_view s) {
    s = trim(s);
    return s.empty() ? std::nullopt : std::optional<std::string>(std::string(s));
}

std::optional<bool> flag(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    return s == "-1" || s == "1";
}

}  // namespace

std::optional<SbsMessage> parseSbsLine(std::string_view line) {
    line = trim(line);
    std::vector<std::string_view> f;
    for (size_t pos = 0;;) {
        const size_t comma = line.find(',', pos);
        f.push_back(line.substr(pos, comma == std::string_view::npos ? comma : comma - pos));
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    if (f.size() < 22 || f[0] != "MSG") return std::nullopt;

    SbsMessage msg;
    const std::string hex(f[4]);
    char* end = nullptr;
    const unsigned long icao = std::strtoul(hex.c_str(), &end, 16);
    if (hex.empty() || *end != '\0' || icao > 0xFFFFFF) return std::nullopt;
    msg.icao = static_cast<uint32_t>(icao);

    TrackFields& t = msg.fields;
    const std::string_view kind = f[1];
    if (kind == "1") {
        t.callsign = text(f[10]);
    } else if (kind == "2") {
        t.altBaroFt = integer(f[11]); t.groundSpeedKt = number(f[12]); t.trackDeg = number(f[13]);
        t.lat = number(f[14]); t.lon = number(f[15]); t.onGround = flag(f[21]);
    } else if (kind == "3") {
        t.altBaroFt = integer(f[11]); t.lat = number(f[14]); t.lon = number(f[15]); t.onGround = flag(f[21]);
    } else if (kind == "4") {
        t.groundSpeedKt = number(f[12]); t.trackDeg = number(f[13]); t.vrateFpm = integer(f[16]);
    } else if (kind == "5" || kind == "6") {
        t.altBaroFt = integer(f[11]);
        if (kind == "6") t.squawk = text(f[17]);
    } else if (kind == "7") {
        t.altBaroFt = integer(f[11]);
    } else if (kind == "8") {
        t.onGround = flag(f[21]);
    } else {
        return std::nullopt;
    }
    return msg;
}

}  // namespace dolo
