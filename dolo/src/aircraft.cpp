#include "aircraft.hpp"

#include <algorithm>
#include <cmath>

#include "geo.hpp"

namespace dolo {
namespace {

template <typename T>
void merge(std::optional<T>& dst, const std::optional<T>& src) {
    if (src) dst = src;
}

}  // namespace

void AircraftStore::update(uint32_t icao, const TrackFields& in, double now) {
    Track& t = tracks_[icao];
    t.lastSeen = now;
    ++messagesTotal_;
    TrackFields& f = t.f;
    merge(f.callsign, in.callsign); merge(f.squawk, in.squawk);
    merge(f.lat, in.lat); merge(f.lon, in.lon);
    merge(f.groundSpeedKt, in.groundSpeedKt); merge(f.trackDeg, in.trackDeg);
    merge(f.altBaroFt, in.altBaroFt); merge(f.altGeomFt, in.altGeomFt); merge(f.vrateFpm, in.vrateFpm);
    if (in.onGround) t.onGround = *in.onGround;
    if (in.lat && in.lon) t.lastPos = now;
}

void AircraftStore::prune(double now) {
    std::erase_if(tracks_, [&](const auto& kv) { return now - kv.second.lastSeen > pruneAfterS_; });
}

solodolo::v1::TrafficSnapshot AircraftStore::snapshot(const std::optional<Ownship>& own, double now) {
    prune(now);
    solodolo::v1::TrafficSnapshot snap;
    snap.set_messages_total(messagesTotal_);
    for (const auto& [icao, t] : tracks_) {
        const TrackFields& f = t.f;
        auto* a = snap.add_aircraft();
        a->set_icao_address(icao);
        a->set_on_ground(t.onGround);
        a->set_seen_s(static_cast<float>(std::max(0.0, now - t.lastSeen)));
        if (f.callsign) a->set_callsign(*f.callsign);
        if (f.squawk) a->set_squawk(*f.squawk);
        if (f.lat && f.lon) {
            a->set_latitude_deg(*f.lat);
            a->set_longitude_deg(*f.lon);
            if (t.lastPos) a->set_seen_pos_s(static_cast<float>(std::max(0.0, now - *t.lastPos)));
            if (own) {
                const auto [range, bearing] = geo::rangeBearing(own->lat, own->lon, *f.lat, *f.lon);
                a->set_range_nm(static_cast<float>(range));
                a->set_bearing_true_deg(static_cast<float>(bearing));
            }
        }
        if (f.altBaroFt) {
            a->set_altitude_baro_ft(*f.altBaroFt);
            if (own && own->altMslFt) a->set_relative_alt_ft(static_cast<int32_t>(std::lround(*f.altBaroFt - *own->altMslFt)));
        }
        if (f.altGeomFt) a->set_altitude_geom_ft(*f.altGeomFt);
        if (f.groundSpeedKt) a->set_ground_speed_kt(static_cast<float>(*f.groundSpeedKt));
        if (f.trackDeg) a->set_track_true_deg(static_cast<float>(*f.trackDeg));
        if (f.vrateFpm) a->set_vertical_rate_fpm(*f.vrateFpm);
    }
    return snap;
}

}  // namespace dolo
