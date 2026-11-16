// adsb/: great-circle geometry, SBS parsing, and the aircraft store's snapshots.
#include "adsb/aircraft.hpp"
#include "adsb/geo.hpp"
#include "adsb/sbs.hpp"
#include "support.hpp"

TEST(Geo, GreatCircleRangeAndBearing) {
    const auto [range, bearing] = geo::rangeBearing(0, 0, 0, 1);
    EXPECT_NEAR(range, 60.04, 0.1);
    EXPECT_NEAR(bearing, 90, 1e-6);
}

static const char* kSbs1 = "MSG,1,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,UAL123  ,,,,,,,,,,,";
static const char* kSbs3 = "MSG,3,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,,35000,,,40.0,-75.0,,,0,0,0,0";
static const char* kSbs4 = "MSG,4,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,,,450.5,270.0,,,-640,,,,,";

TEST(AircraftStore, SbsMessagesBecomeSnapshots) {
    AircraftStore store(60);
    double t = 10;
    for (const char* line : {kSbs1, kSbs3, kSbs4}) {
        const auto m = parseSbsLine(line);
        EXPECT_TRUE(m.has_value());
        store.update(m->icao, m->fields, t++);
    }
    const auto snap = store.snapshot(Ownship{40.0, -76.0, 1000.0}, 13.0);
    EXPECT_EQ(snap.aircraft_size(), 1);
    EXPECT_EQ(snap.messages_total(), 3);
    const auto& a = snap.aircraft(0);
    EXPECT_EQ(a.icao_address(), 0xA1B2C3);
    EXPECT_EQ(a.callsign(), "UAL123");
    EXPECT_EQ(a.altitude_baro_ft(), 35000);
    EXPECT_EQ(a.relative_alt_ft(), 34000);
    EXPECT_NEAR(a.ground_speed_kt(), 450.5, 1e-3);
    EXPECT_EQ(a.vertical_rate_fpm(), -640);
    EXPECT_TRUE(a.range_nm() > 40);
    EXPECT_TRUE(a.range_nm() < 50);
    EXPECT_NEAR(a.bearing_true_deg(), 90, 1);
    EXPECT_FALSE(a.has_squawk());

    EXPECT_FALSE(store.snapshot(std::nullopt, 13.0).aircraft(0).has_range_nm());
    EXPECT_EQ(store.snapshot(std::nullopt, 100.0).aircraft_size(), 0);  // pruned after 60 s

    EXPECT_FALSE(parseSbsLine("garbage"));
    EXPECT_FALSE(parseSbsLine("MSG,3,1,1,ZZZZZZ,,,,,,,,,,,,,,,,,,"));
}
