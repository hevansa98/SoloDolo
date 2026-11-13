// Per-ICAO aircraft state merged from SBS messages.
#pragma once

#include <map>
#include <optional>

#include "sbs.hpp"
#include "solodolo.pb.h"

namespace dolo {

struct Ownship {
    double lat, lon;
    std::optional<double> altMslFt;
};

struct Track {
    TrackFields f;
    bool onGround = false;
    double lastSeen = 0;
    std::optional<double> lastPos;
};

class AircraftStore {
public:
    explicit AircraftStore(double pruneAfterS = 60.0) : pruneAfterS_(pruneAfterS) {}

    /// Merges the present fields into the track for `icao`, creating it if needed.
    void update(uint32_t icao, const TrackFields& fields, double now);
    void prune(double now);
    solodolo::v1::TrafficSnapshot snapshot(const std::optional<Ownship>& ownship, double now);

    size_t size() const { return tracks_.size(); }
    uint64_t messagesTotal() const { return messagesTotal_; }

private:
    double pruneAfterS_;
    uint64_t messagesTotal_ = 0;
    std::map<uint32_t, Track> tracks_;  // ordered, so snapshots are sorted by ICAO
};

}  // namespace dolo
