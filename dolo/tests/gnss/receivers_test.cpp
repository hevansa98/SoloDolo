// gnss/ receivers: gpsd, GT-U7, and the MAX-M8Q over both UBX and NMEA, fed with captured data.
#include <cstdio>
#include <string>
#include <vector>

#include "core/event_loop.hpp"
#include "fixtures.hpp"
#include "gnss/gpsd.hpp"
#include "gnss/gt_u7.hpp"
#include "gnss/max_m8q.hpp"
#include "gnss/stream.hpp"
#include "support.hpp"

TEST(GpsdReceiver, MergesTpvAndSky) {
    EventLoop loop;
    GpsdReceiver rx(loop, "127.0.0.1", 2947);  // never started: only the message handling is under test
    EXPECT_FALSE(rx.state().ownship());
    EXPECT_EQ(rx.state().toProto().fix_mode(), pb::FIX_MODE_NO_FIX);
    rx.apply(nlohmann::json::parse(R"({"class":"SKY","hdop":0.9,"satellites":[{"used":true},{"used":false}]})"));
    const bool tpv = rx.apply(nlohmann::json::parse(
        R"({"class":"TPV","mode":3,"time":"2026-01-01T00:00:00.500Z","lat":40.0,"lon":-75.0,"altMSL":100.0,"speed":10.0,"eph":3.0})"));
    EXPECT_TRUE(tpv);
    const auto f = rx.state().toProto();
    EXPECT_EQ(f.fix_mode(), pb::FIX_MODE_3D);
    EXPECT_EQ(f.satellites_used(), 1);
    EXPECT_EQ(f.satellites_visible(), 2);
    EXPECT_EQ(f.fix_unix_ms(), 1767225600500ULL);
    EXPECT_NEAR(f.ground_speed_kt(), 19.44, 0.01);
    EXPECT_NEAR(*rx.state().ownship()->altMslFt, 328.084, 1e-3);
    rx.apply(nlohmann::json::parse(R"({"class":"TPV","mode":1})"));
    EXPECT_FALSE(rx.state().ownship());
}

TEST(GtU7Receiver, BuildsFixFromNmea) {
    EventLoop loop;
    GtU7Receiver rx(loop, "/dev/null");  // never started: sentences are fed directly
    int fixes = 0;
    rx.onFix = [&](const GnssState&) { ++fixes; };
    EXPECT_EQ(rx.model(), "u-blox GT-U7");

    // Real cold-start output from the GT-U7: no fix yet.
    EXPECT_FALSE(rx.feed("$GPRMC,,V,,,,,,,,,,N*53\r\n"));
    EXPECT_TRUE(rx.feed("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n"));
    EXPECT_EQ(fixes, 1);
    EXPECT_FALSE(rx.state().ownship());
    EXPECT_EQ(rx.state().toProto().fix_mode(), pb::FIX_MODE_NO_FIX);
    EXPECT_EQ(rx.state().used, 0);

    // A fix: 40 deg 30.000' N, 75 deg 15.000' W, 100.0 m MSL, 10 kt, track 90.
    EXPECT_FALSE(rx.feed(sentence("GPGSV,1,1,07,01,40,083,46,02,17,308,41")));
    EXPECT_FALSE(rx.feed(sentence("GPRMC,010203.50,A,4030.000,N,07515.000,W,10.0,90.0,010126,,,A")));
    EXPECT_FALSE(rx.feed(sentence("GPGSA,A,3,01,02,,,,,,,,,,,1.8,0.9,1.5")));
    EXPECT_TRUE(rx.feed(sentence("GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")));
    EXPECT_EQ(fixes, 2);
    EXPECT_NEAR(*rx.state().lat, 40.5, 1e-9);
    EXPECT_NEAR(*rx.state().lon, -75.25, 1e-9);
    const auto f = rx.state().toProto();
    EXPECT_EQ(f.fix_mode(), pb::FIX_MODE_3D);
    EXPECT_EQ(f.satellites_used(), 7);
    EXPECT_EQ(f.satellites_visible(), 7);
    EXPECT_EQ(f.fix_unix_ms(), 1767229323500ULL);  // 2026-01-01T01:02:03.500Z
    EXPECT_NEAR(f.altitude_msl_m(), 100.0, 1e-3);
    EXPECT_NEAR(f.ground_speed_kt(), 10.0, 1e-3);
    EXPECT_NEAR(f.track_true_deg(), 90.0, 1e-3);
    EXPECT_NEAR(f.hdop(), 0.9, 1e-3);
    EXPECT_NEAR(f.vdop(), 1.5, 1e-3);
    EXPECT_TRUE(rx.state().ownship());
    EXPECT_TRUE(rx.state().ownship()->altMslFt);

    // A 2D fix from GSA wins over GGA's altitude.
    rx.feed(sentence("GPGSA,A,2,01,02,,,,,,,,,,,1.8,0.9,1.5"));
    rx.feed(sentence("GPGGA,010204.00,4030.000,N,07515.000,W,1,04,0.9,100.0,M,-34.0,M,,"));
    EXPECT_EQ(rx.state().toProto().fix_mode(), pb::FIX_MODE_2D);
    EXPECT_FALSE(rx.state().toProto().has_altitude_msl_m());

    // Bad input is ignored; an unusable latitude is reported as no fix.
    EXPECT_FALSE(rx.feed("$GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,*00"));  // bad checksum
    EXPECT_TRUE(rx.feed(sentence("GPGGA,010203.50,9900.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")));
    EXPECT_FALSE(rx.state().ownship());

    // Losing the fix clears the position again.
    EXPECT_TRUE(rx.feed("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n"));
    EXPECT_FALSE(rx.state().ownship());
}

TEST(MaxM8qReceiver, CombinesConstellationsFromNmea) {
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    GnssState last;
    rx.onFix = [&](const GnssState& s) { last = s; };
    EXPECT_EQ(rx.model(), "u-blox MAX-M8Q");

    // One epoch captured from the MAX-M8Q HAT: GPS + GLONASS, one GSA and one GSV group per constellation.
    const char* epoch[] = {
        "$GNRMC,025650.00,A,3302.68922,N,09659.30219,W,0.032,,151126,,,A*76",
        "$GNVTG,,T,,M,0.032,N,0.060,K,A*3A",
        "$GNGGA,025650.00,3302.68922,N,09659.30219,W,1,12,0.83,142.3,M,-25.2,M,,*7F",
        "$GNGSA,A,3,06,14,13,11,12,17,19,22,24,,,,1.72,0.83,1.51*1C",
        "$GNGSA,A,3,74,83,84,80,,,,,,,,,1.72,0.83,1.51*1A",
        "$GPGSV,3,1,12,03,01,030,,06,70,024,33,11,70,245,35,12,49,317,32*76",
        "$GLGSV,2,1,05,73,75,263,27,74,31,327,27,80,34,164,34,83,57,358,30*6A",
    };
    for (const char* line : epoch) rx.feed(line);  // first epoch: GGA publishes before this epoch's GSA/GSV arrive
    for (const char* line : epoch) rx.feed(line);  // second epoch: the report carries the first epoch's satellite data
    EXPECT_EQ(last.mode, 3);
    EXPECT_EQ(last.used, 12);
    EXPECT_EQ(last.visible, 17);  // 12 GPS + 5 GLONASS
    EXPECT_NEAR(*last.lat, 33.0 + 2.68922 / 60.0, 1e-9);
    EXPECT_NEAR(*last.lon, -(96.0 + 59.30219 / 60.0), 1e-9);
    EXPECT_NEAR(*last.altM, 142.3, 1e-9);
    EXPECT_NEAR(*last.hdop, 0.83, 1e-9);
    EXPECT_NEAR(*last.vdop, 1.51, 1e-9);
    EXPECT_EQ(last.timeMs, 1794711410000ULL);  // 2026-11-15T02:56:50Z

    // The best fix type across an epoch's GSAs wins, whichever order they arrive in.
    const std::string gsa2d = sentence("GNGSA,A,2,06,14,13,11,12,17,19,22,24,,,,1.72,0.83,1.51");
    EXPECT_TRUE(nmea::parse(gsa2d));  // guard: the sentence must be valid, or the checks below prove nothing
    rx.feed(epoch[0]);
    rx.feed(gsa2d);
    rx.feed(epoch[4]);  // 3D after 2D
    rx.feed(epoch[2]);
    EXPECT_EQ(last.mode, 3);
    rx.feed(epoch[0]);
    rx.feed(epoch[4]);
    rx.feed(gsa2d);  // 2D after 3D
    rx.feed(epoch[2]);
    EXPECT_EQ(last.mode, 3);
    rx.feed(epoch[0]);
    rx.feed(gsa2d);  // a lone 2D report is 2D
    rx.feed(epoch[2]);
    EXPECT_EQ(last.mode, 2);

    // A constellation that drops out of view stops counting.
    rx.feed(epoch[0]);
    rx.feed(epoch[5]);  // GPS only this epoch
    rx.feed(epoch[2]);
    EXPECT_EQ(last.visible, 12);
}

TEST(MaxM8qReceiver, ReportsEachCompleteUbxEpoch) {
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    std::vector<GnssState> reports;
    rx.onFix = [&](const GnssState &s) { reports.push_back(s); };

    // The module sends PVT, SAT, DOP in that order; a report goes out when the epoch is complete.
    rx.feedBytes(kRealNavPvt);
    EXPECT_TRUE(reports.empty());
    rx.feedBytes(navSat(15554000, 25, 16));
    EXPECT_TRUE(reports.empty());
    rx.feedBytes(kRealNavDop);
    EXPECT_EQ(reports.size(), 1);
    const GnssState &g = reports.back();
    EXPECT_EQ(g.mode, 3);
    EXPECT_EQ(g.used, 16);
    EXPECT_EQ(g.visible, 25);
    EXPECT_NEAR(*g.lat, 33.0448126, 1e-9);
    EXPECT_NEAR(*g.lon, -96.9884181, 1e-9);
    EXPECT_NEAR(*g.altM, 144.195, 1e-9);
    EXPECT_NEAR(*g.hdop, 0.70, 1e-9);
    EXPECT_NEAR(*g.vdop, 1.30, 1e-9);
    EXPECT_NEAR(*g.eph, 2.432, 1e-9);  // the field NMEA cannot provide
    EXPECT_NEAR(*g.speedKt, 0.0525, 1e-3);
    EXPECT_NEAR(*g.climbFpm, -17.72, 1e-2);
    EXPECT_FALSE(g.track);  // stationary: the module's course over ground is meaningless
    EXPECT_EQ(g.timeMs, 1794716336000ULL);
    const auto proto = g.toProto();
    EXPECT_EQ(proto.fix_mode(), pb::FIX_MODE_3D);
    EXPECT_TRUE(proto.has_horizontal_error_m());
    EXPECT_TRUE(proto.has_climb_fpm());

    // Any order works: the next epoch arrives DOP, SAT, PVT.
    rx.feedBytes(atItow(kRealNavDop, 15555000));
    rx.feedBytes(navSat(15555000, 22, 12));
    EXPECT_EQ(reports.size(), 1);
    rx.feedBytes(atItow(kRealNavPvt, 15555000));
    EXPECT_EQ(reports.size(), 2);
    EXPECT_EQ(reports.back().visible, 22);

    // An epoch that never completes (SAT lost) is reported when the next epoch starts, not dropped.
    rx.feedBytes(atItow(kRealNavPvt, 15556000));
    EXPECT_EQ(reports.size(), 2);
    rx.feedBytes(atItow(kRealNavPvt, 15557000));
    EXPECT_EQ(reports.size(), 3);

    // While UBX is flowing, NMEA position sentences are ignored.
    EXPECT_FALSE(rx.feed("$GNRMC,025650.00,A,3302.68922,N,09659.30219,W,0.032,,151126,,,A*76\r\n"));
    EXPECT_FALSE(rx.feed("$GNGGA,025650.00,3302.68922,N,09659.30219,W,1,12,0.83,142.3,M,-25.2,M,,*7F\r\n"));
    EXPECT_EQ(reports.size(), 3);
    EXPECT_NEAR(*rx.state().lat, 33.0448126, 1e-9);

    // A NAK and unrelated frames are harmless.
    rx.feedBytes(ubx::encode(ubx::kClassAck, ubx::kAckNak, std::string("\x06\x01", 2)));
    rx.feedBytes(ubx::encode(0x0a, 0x04, "ver"));
    EXPECT_EQ(reports.size(), 3);

    // The module loses its fix.
    std::string noFix = kRealNavPvt.substr(6, kRealNavPvt.size() - 8);
    noFix[20] = 0;  // fixType none
    noFix[21] = 0;  // gnssFixOK cleared
    putU32(noFix, 0, 15558000);
    rx.feedBytes(ubx::encode(ubx::kClassNav, ubx::kNavPvt, noFix));
    rx.feedBytes(atItow(kRealNavDop, 15558000));
    rx.feedBytes(navSat(15558000, 5, 0));
    EXPECT_EQ(reports.size(), 5);  // the incomplete 15557000 epoch is flushed first, then this one completes
    EXPECT_EQ(reports.back().mode, 1);
    EXPECT_FALSE(rx.state().ownship());
    EXPECT_FALSE(reports.back().eph);
}

TEST(MaxM8qReceiver, FallsBackToNmeaWhenUbxStops) {
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    int fixes = 0;
    rx.onFix = [&](const GnssState &) { ++fixes; };
    const char *rmc = "$GNRMC,025650.00,A,3302.68922,N,09659.30219,W,0.032,,151126,,,A*76\r\n";
    const char *gga = "$GNGGA,025650.00,3302.68922,N,09659.30219,W,1,12,0.83,142.3,M,-25.2,M,,*7F\r\n";

    // Never saw UBX (TX wire not connected, say): NMEA is the fix from the first epoch.
    rx.feed(rmc);
    EXPECT_TRUE(rx.feed(gga));
    EXPECT_EQ(fixes, 1);

    // UBX arrives and takes over...
    rx.feedBytes(kRealNavPvt);
    rx.feedBytes(kRealNavDop);
    rx.feedBytes(navSat(15554000, 25, 16));
    EXPECT_EQ(fixes, 2);
    EXPECT_TRUE(rx.state().eph);
    for (int epoch = 0; epoch < 2; ++epoch)
    {
        rx.feed(rmc);
        EXPECT_FALSE(rx.feed(gga));  // within the grace period
    }
    EXPECT_EQ(fixes, 2);

    // ...then goes silent (the module reset): after the grace period NMEA is trusted again and the UBX-only
    // fields stop being reported.
    rx.feed(rmc);
    EXPECT_TRUE(rx.feed(gga));
    EXPECT_EQ(fixes, 3);
    EXPECT_NEAR(*rx.state().lat, 33.0 + 2.68922 / 60.0, 1e-9);
    EXPECT_FALSE(rx.state().eph);
    EXPECT_FALSE(rx.state().climbFpm);

    // The startup commands switch on exactly the UBX messages we read and switch off the redundant NMEA ones.
    std::vector<std::string> cmds;
    StreamSplitter split;
    split.onUbx = [&](const ubx::Frame &f)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02x-%02x=%d", static_cast<int>(f.payload[0]), static_cast<int>(f.payload[1]),
                      static_cast<int>(f.payload[2]));
        EXPECT_EQ(f.cls, ubx::kClassCfg);
        EXPECT_EQ(f.id, ubx::kCfgMsg);
        EXPECT_EQ(f.payload.size(), 3);
        cmds.push_back(buf);
    };
    split.feed(MaxM8qReceiver::configurationCommands());
    const std::vector<std::string> want = {"01-07=1", "01-04=1", "01-35=1", "f0-01=0", "f0-02=0", "f0-03=0", "f0-05=0"};
    EXPECT_EQ(cmds, want);  // GGA (f0-00) and RMC (f0-04) stay on as the fallback
}
