// Great-circle range and bearing.
#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

namespace dolo::geo {

constexpr double kEarthRadiusNm = 3440.065;

/// Range in nautical miles and initial true bearing (0-360) from point 1 to point 2.
inline std::pair<double, double> rangeBearing(double lat1, double lon1, double lat2, double lon2) {
    constexpr double rad = M_PI / 180.0;
    const double p1 = lat1 * rad, p2 = lat2 * rad, dlon = (lon2 - lon1) * rad;
    const double s1 = std::sin((p2 - p1) / 2), s2 = std::sin(dlon / 2);
    const double a = s1 * s1 + std::cos(p1) * std::cos(p2) * s2 * s2;
    const double range = 2 * kEarthRadiusNm * std::asin(std::min(1.0, std::sqrt(a)));
    const double y = std::sin(dlon) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dlon);
    return {range, std::fmod(std::atan2(y, x) / rad + 360.0, 360.0)};
}

}  // namespace dolo::geo
