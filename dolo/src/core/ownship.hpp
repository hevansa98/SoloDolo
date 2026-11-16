// Our own position: produced by a GNSS receiver, consumed when computing range and bearing to traffic.
#pragma once

#include <optional>

namespace dolo {

struct Ownship {
    double lat, lon;
    std::optional<double> altMslFt;
};

}  // namespace dolo
