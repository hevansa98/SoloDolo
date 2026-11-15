// Dependency-free tests: build and run `dolo_tests`.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#include "aircraft.hpp"
#include "config.hpp"
#include "framing.hpp"
#include "geo.hpp"
#include "gnss/gpsd.hpp"
#include "gnss/gt_u7.hpp"
#include "gnss/max_m8q.hpp"
#include "gnss/nmea.hpp"
#include "gnss/stream.hpp"
#include "gnss/ubx.hpp"
#include "hub.hpp"
#include "sbs.hpp"

using namespace dolo;
namespace pb = solodolo::v1;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))
#define CHECK_THROWS(expr) do { bool threw = false; try { expr; } catch (const std::exception&) { threw = true; } CHECK(threw); } while (0)

static void testFraming() {
    CHECK(encodeFrame("abc") == std::string("\x00\x00\x00\x03" "abc", 7));
    CHECK_THROWS(encodeFrame(std::string(kMaxFrameBytes + 1, 'x')));

    std::string buf = encodeFrame("one") + encodeFrame("two") + std::string("\x00\x00", 2), out;
    CHECK(extractFrame(buf, out) == FrameStatus::Frame && out == "one");
    CHECK(extractFrame(buf, out) == FrameStatus::Frame && out == "two");
    CHECK(extractFrame(buf, out) == FrameStatus::NeedMore);
    std::string huge("\x00\x10\x00\x01", 4);  // 1 MiB + 1
    CHECK(extractFrame(huge, out) == FrameStatus::Bad);
}

static void testGeo() {
    const auto [range, bearing] = geo::rangeBearing(0, 0, 0, 1);
    NEAR(range, 60.04, 0.1);
    NEAR(bearing, 90, 1e-6);
}

static const char* kSbs1 = "MSG,1,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,UAL123  ,,,,,,,,,,,";
static const char* kSbs3 = "MSG,3,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,,35000,,,40.0,-75.0,,,0,0,0,0";
static const char* kSbs4 = "MSG,4,1,1,A1B2C3,1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,,,450.5,270.0,,,-640,,,,,";

static void testSbsAndSnapshot() {
    AircraftStore store(60);
    double t = 10;
    for (const char* line : {kSbs1, kSbs3, kSbs4}) {
        const auto m = parseSbsLine(line);
        CHECK(m.has_value());
        store.update(m->icao, m->fields, t++);
    }
    const auto snap = store.snapshot(Ownship{40.0, -76.0, 1000.0}, 13.0);
    CHECK(snap.aircraft_size() == 1 && snap.messages_total() == 3);
    const auto& a = snap.aircraft(0);
    CHECK(a.icao_address() == 0xA1B2C3 && a.callsign() == "UAL123");
    CHECK(a.altitude_baro_ft() == 35000 && a.relative_alt_ft() == 34000);
    NEAR(a.ground_speed_kt(), 450.5, 1e-3);
    CHECK(a.vertical_rate_fpm() == -640);
    CHECK(a.range_nm() > 40 && a.range_nm() < 50);
    NEAR(a.bearing_true_deg(), 90, 1);
    CHECK(!a.has_squawk());

    CHECK(!store.snapshot(std::nullopt, 13.0).aircraft(0).has_range_nm());
    CHECK(store.snapshot(std::nullopt, 100.0).aircraft_size() == 0);  // pruned after 60 s

    CHECK(!parseSbsLine("garbage"));
    CHECK(!parseSbsLine("MSG,3,1,1,ZZZZZZ,,,,,,,,,,,,,,,,,,"));
}

static void testGpsd() {
    EventLoop loop;
    GpsdReceiver rx(loop, "127.0.0.1", 2947);  // never started: only the message handling is under test
    CHECK(!rx.state().ownship() && rx.state().toProto().fix_mode() == pb::FIX_MODE_NO_FIX);
    rx.apply(nlohmann::json::parse(R"({"class":"SKY","hdop":0.9,"satellites":[{"used":true},{"used":false}]})"));
    const bool tpv = rx.apply(nlohmann::json::parse(
        R"({"class":"TPV","mode":3,"time":"2026-01-01T00:00:00.500Z","lat":40.0,"lon":-75.0,"altMSL":100.0,"speed":10.0,"eph":3.0})"));
    CHECK(tpv);
    const auto f = rx.state().toProto();
    CHECK(f.fix_mode() == pb::FIX_MODE_3D && f.satellites_used() == 1 && f.satellites_visible() == 2);
    CHECK(f.fix_unix_ms() == 1767225600500ULL);
    NEAR(f.ground_speed_kt(), 19.44, 0.01);
    NEAR(*rx.state().ownship()->altMslFt, 328.084, 1e-3);
    rx.apply(nlohmann::json::parse(R"({"class":"TPV","mode":1})"));
    CHECK(!rx.state().ownship());
}

static std::string sentence(const std::string& body) {
    unsigned sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return "$" + body + tail;
}

static void testNmeaParse() {
    const auto gga = nmea::parse(sentence("GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,"));
    CHECK(gga && std::holds_alternative<nmea::Gga>(*gga));
    const auto& g = std::get<nmea::Gga>(*gga);
    NEAR(*g.lat, 40.5, 1e-9);
    NEAR(*g.lon, -75.25, 1e-9);
    CHECK(g.quality == 1 && g.satsUsed == 7);

    CHECK(!nmea::parse("$GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,*00"));  // bad checksum
    CHECK(!nmea::parse("GPGGA,no dollar sign"));
    CHECK(!nmea::parse(""));
    CHECK(!nmea::parse(sentence("GPVTG,,T,,M,0.0,N,0.0,K,A")));  // a sentence Dolo does not use
    const auto bad = nmea::parse(sentence("GPGGA,010203.50,9900.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,"));
    CHECK(bad && !std::get<nmea::Gga>(*bad).lat);  // latitude out of range is dropped
}

static void testGtU7() {
    EventLoop loop;
    GtU7Receiver rx(loop, "/dev/null");  // never started: sentences are fed directly
    int fixes = 0;
    rx.onFix = [&](const GnssState&) { ++fixes; };
    CHECK(rx.model() == "u-blox GT-U7");

    // Real cold-start output from the GT-U7: no fix yet.
    CHECK(!rx.feed("$GPRMC,,V,,,,,,,,,,N*53\r\n"));
    CHECK(rx.feed("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n") && fixes == 1);
    CHECK(!rx.state().ownship() && rx.state().toProto().fix_mode() == pb::FIX_MODE_NO_FIX && rx.state().used == 0);

    // A fix: 40 deg 30.000' N, 75 deg 15.000' W, 100.0 m MSL, 10 kt, track 90.
    CHECK(!rx.feed(sentence("GPGSV,1,1,07,01,40,083,46,02,17,308,41")));
    CHECK(!rx.feed(sentence("GPRMC,010203.50,A,4030.000,N,07515.000,W,10.0,90.0,010126,,,A")));
    CHECK(!rx.feed(sentence("GPGSA,A,3,01,02,,,,,,,,,,,1.8,0.9,1.5")));
    CHECK(rx.feed(sentence("GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")) && fixes == 2);
    NEAR(*rx.state().lat, 40.5, 1e-9);
    NEAR(*rx.state().lon, -75.25, 1e-9);
    const auto f = rx.state().toProto();
    CHECK(f.fix_mode() == pb::FIX_MODE_3D && f.satellites_used() == 7 && f.satellites_visible() == 7);
    CHECK(f.fix_unix_ms() == 1767229323500ULL);  // 2026-01-01T01:02:03.500Z
    NEAR(f.altitude_msl_m(), 100.0, 1e-3);
    NEAR(f.ground_speed_kt(), 10.0, 1e-3);
    NEAR(f.track_true_deg(), 90.0, 1e-3);
    NEAR(f.hdop(), 0.9, 1e-3);
    NEAR(f.vdop(), 1.5, 1e-3);
    CHECK(rx.state().ownship() && rx.state().ownship()->altMslFt);

    // A 2D fix from GSA wins over GGA's altitude.
    rx.feed(sentence("GPGSA,A,2,01,02,,,,,,,,,,,1.8,0.9,1.5"));
    rx.feed(sentence("GPGGA,010204.00,4030.000,N,07515.000,W,1,04,0.9,100.0,M,-34.0,M,,"));
    CHECK(rx.state().toProto().fix_mode() == pb::FIX_MODE_2D && !rx.state().toProto().has_altitude_msl_m());

    // Bad input is ignored; an unusable latitude is reported as no fix.
    CHECK(!rx.feed("$GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,*00"));  // bad checksum
    CHECK(rx.feed(sentence("GPGGA,010203.50,9900.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")) && !rx.state().ownship());

    // Losing the fix clears the position again.
    CHECK(rx.feed("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n") && !rx.state().ownship());
}

static void testMaxM8q() {
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    GnssState last;
    rx.onFix = [&](const GnssState& s) { last = s; };
    CHECK(rx.model() == "u-blox MAX-M8Q");

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
    CHECK(last.mode == 3 && last.used == 12);
    CHECK(last.visible == 17);  // 12 GPS + 5 GLONASS
    NEAR(*last.lat, 33.0 + 2.68922 / 60.0, 1e-9);
    NEAR(*last.lon, -(96.0 + 59.30219 / 60.0), 1e-9);
    NEAR(*last.altM, 142.3, 1e-9);
    NEAR(*last.hdop, 0.83, 1e-9);
    NEAR(*last.vdop, 1.51, 1e-9);
    CHECK(last.timeMs == 1794711410000ULL);  // 2026-11-15T02:56:50Z

    // The best fix type across an epoch's GSAs wins, whichever order they arrive in.
    const std::string gsa2d = sentence("GNGSA,A,2,06,14,13,11,12,17,19,22,24,,,,1.72,0.83,1.51");
    CHECK(nmea::parse(gsa2d));  // guard: the sentence must be valid, or the checks below prove nothing
    rx.feed(epoch[0]);
    rx.feed(gsa2d);
    rx.feed(epoch[4]);  // 3D after 2D
    rx.feed(epoch[2]);
    CHECK(last.mode == 3);
    rx.feed(epoch[0]);
    rx.feed(epoch[4]);
    rx.feed(gsa2d);  // 2D after 3D
    rx.feed(epoch[2]);
    CHECK(last.mode == 3);
    rx.feed(epoch[0]);
    rx.feed(gsa2d);  // a lone 2D report is 2D
    rx.feed(epoch[2]);
    CHECK(last.mode == 2);

    // A constellation that drops out of view stops counting.
    rx.feed(epoch[0]);
    rx.feed(epoch[5]);  // GPS only this epoch
    rx.feed(epoch[2]);
    CHECK(last.visible == 12);
}

// ---- UBX ----------------------------------------------------------------------------------------------------

// Frames captured from the MAX-M8Q HAT (GPS time of week 15554000, stationary, 3D fix with 16 satellites).
static const std::string kRealNavPvt(
    "\xb5\x62\x01\x07\x5c\x00\xd0\x55\xed\x00\xea\x07\x0b\x0f\x04\x12\x38\x37\x3a\x00\x00\x00\x96\x48\x07\x00\x03\x03\x0a\x10"
    "\xeb\xbd\x30\xc6\xfe\x3c\xb2\x13\xe7\xd0\x01\x00\x43\x33\x02\x00\x80\x09\x00\x00\xeb\x11\x00\x00\x18\x00\x00\x00\xf3\xff"
    "\xff\xff\x5a\x00\x00\x00\x1b\x00\x00\x00\x00\x00\x00\x00\xac\x01\x00\x00\x0c\xe9\x08\x01\x93\x00\x00\x00\xe0\x4a\x23\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x9f\x42",
    100);
static const std::string kRealNavDop(
    "\xb5\x62\x01\x04\x12\x00\xd0\x55\xed\x00\xab\x00\x93\x00\x57\x00\x82\x00\x46\x00\x33\x00\x30\x00\xe9\x6b", 26);

static void putU32(std::string &s, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}

/// The same frame for a different epoch: patches iTOW and re-encodes so the checksum stays valid.
static std::string atItow(const std::string &frame, uint32_t iTow)
{
    std::string payload = frame.substr(6, frame.size() - 8);
    putU32(payload, 0, iTow);
    return ubx::encode(static_cast<uint8_t>(frame[2]), static_cast<uint8_t>(frame[3]), payload);
}

/// UBX-NAV-SAT with `n` satellites, the first `used` of them flagged as used in the solution.
static std::string navSat(uint32_t iTow, int n, int used)
{
    std::string payload(8 + 12 * n, '\0');
    putU32(payload, 0, iTow);
    payload[4] = 1;  // version
    payload[5] = static_cast<char>(n);
    for (int i = 0; i < n; ++i) payload[8 + 12 * i + 8] = i < used ? '\x0f' : '\x07';  // bit 3 is svUsed
    return ubx::encode(ubx::kClassNav, ubx::kNavSat, payload);
}

static void testUbx()
{
    CHECK(ubx::cfgMsg(0x01, 0x07, 1) == std::string("\xb5\x62\x06\x01\x03\x00\x01\x07\x01\x13\x51", 11));  // checksum from an independent calculation

    std::vector<ubx::Frame> frames;
    StreamSplitter split;
    split.onUbx = [&](const ubx::Frame &f) { frames.push_back(f); };
    split.feed(kRealNavPvt + kRealNavDop + navSat(15554000, 25, 16));
    CHECK(frames.size() == 3);

    const auto pvt = ubx::decodeNavPvt(frames[0]);
    CHECK(pvt && pvt->iTow == 15554000 && pvt->fixType == 3 && pvt->fixOk && pvt->satsUsed == 16);
    NEAR(pvt->latDeg, 33.0448126, 1e-9);
    NEAR(pvt->lonDeg, -96.9884181, 1e-9);
    NEAR(pvt->hMslM, 144.195, 1e-9);
    NEAR(pvt->hAccM, 2.432, 1e-9);
    NEAR(pvt->groundSpeedMs, 0.027, 1e-9);
    NEAR(pvt->velDownMs, 0.090, 1e-9);
    CHECK(pvt->timeMs == 1794716336000ULL);  // 2026-11-15T04:18:56Z; the module's fraction is 0.48 ms

    const auto dop = ubx::decodeNavDop(frames[1]);
    CHECK(dop && dop->iTow == 15554000);
    NEAR(dop->hdop, 0.70, 1e-9);
    NEAR(dop->vdop, 1.30, 1e-9);

    const auto sat = ubx::decodeNavSat(frames[2]);
    CHECK(sat && sat->visible == 25 && sat->used == 16);

    // Decoders reject the wrong message and truncated payloads.
    CHECK(!ubx::decodeNavPvt(frames[1]) && !ubx::decodeNavDop(frames[0]) && !ubx::decodeNavSat(frames[0]));
    ubx::Frame shortPvt = frames[0];
    shortPvt.payload.resize(91);
    CHECK(!ubx::decodeNavPvt(shortPvt));
    ubx::Frame shortSat = frames[2];
    shortSat.payload.resize(shortSat.payload.size() - 1);
    CHECK(!ubx::decodeNavSat(shortSat));
}

static void testStreamSplitter()
{
    std::vector<std::string> lines;
    std::vector<ubx::Frame> frames;
    StreamSplitter split;
    split.onNmea = [&](std::string_view l) { lines.emplace_back(l); };
    split.onUbx = [&](const ubx::Frame &f) { frames.push_back(f); };

    // A UBX payload full of bytes that look like NMEA framing: '$' and newlines.
    const std::string tricky = ubx::encode(0x0a, 0x04, std::string("$GPGGA,\n\r\n\n$", 12));
    std::string corrupt = ubx::encode(0x0a, 0x05, "xyz");
    corrupt[corrupt.size() - 1] ^= 0x55;  // bad checksum
    const std::string stream = "$GPRMC,one*00\r\n" + tricky + "\x01\x02garbage" + corrupt + std::string("\xb5\x00\xb5", 3) +
                               "$GPGGA,two*00\r\n" + kRealNavDop;

    for (const char c : stream) split.feed(std::string_view(&c, 1));  // worst case chunking: one byte at a time
    CHECK(lines.size() == 2 && lines[0] == "$GPRMC,one*00\r\n" && lines[1] == "$GPGGA,two*00\r\n");
    CHECK(frames.size() == 2);
    CHECK(frames[0].cls == 0x0a && frames[0].id == 0x04 && frames[0].payload == std::string("$GPGGA,\n\r\n\n$", 12));
    CHECK(frames[1].cls == ubx::kClassNav && frames[1].id == ubx::kNavDop);

    // The same stream in one chunk gives the same result.
    lines.clear();
    frames.clear();
    split.feed(stream);
    CHECK(lines.size() == 2 && frames.size() == 2);

    // A '$' with no newline for hundreds of bytes is dropped and the stream recovers.
    lines.clear();
    split.feed("$" + std::string(300, 'x') + "\n$GPGGA,three*00\r\n");
    CHECK(!lines.empty() && lines.back() == "$GPGGA,three*00\r\n");
}

static void testMaxM8qUbx()
{
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    std::vector<GnssState> reports;
    rx.onFix = [&](const GnssState &s) { reports.push_back(s); };

    // The module sends PVT, SAT, DOP in that order; a report goes out when the epoch is complete.
    rx.feedBytes(kRealNavPvt);
    CHECK(reports.empty());
    rx.feedBytes(navSat(15554000, 25, 16));
    CHECK(reports.empty());
    rx.feedBytes(kRealNavDop);
    CHECK(reports.size() == 1);
    const GnssState &g = reports.back();
    CHECK(g.mode == 3 && g.used == 16 && g.visible == 25);
    NEAR(*g.lat, 33.0448126, 1e-9);
    NEAR(*g.lon, -96.9884181, 1e-9);
    NEAR(*g.altM, 144.195, 1e-9);
    NEAR(*g.hdop, 0.70, 1e-9);
    NEAR(*g.vdop, 1.30, 1e-9);
    NEAR(*g.eph, 2.432, 1e-9);  // the field NMEA cannot provide
    NEAR(*g.speedKt, 0.0525, 1e-3);
    NEAR(*g.climbFpm, -17.72, 1e-2);
    CHECK(!g.track);  // stationary: the module's course over ground is meaningless
    CHECK(g.timeMs == 1794716336000ULL);
    const auto proto = g.toProto();
    CHECK(proto.fix_mode() == pb::FIX_MODE_3D && proto.has_horizontal_error_m() && proto.has_climb_fpm());

    // Any order works: the next epoch arrives DOP, SAT, PVT.
    rx.feedBytes(atItow(kRealNavDop, 15555000));
    rx.feedBytes(navSat(15555000, 22, 12));
    CHECK(reports.size() == 1);
    rx.feedBytes(atItow(kRealNavPvt, 15555000));
    CHECK(reports.size() == 2 && reports.back().visible == 22);

    // An epoch that never completes (SAT lost) is reported when the next epoch starts, not dropped.
    rx.feedBytes(atItow(kRealNavPvt, 15556000));
    CHECK(reports.size() == 2);
    rx.feedBytes(atItow(kRealNavPvt, 15557000));
    CHECK(reports.size() == 3);

    // While UBX is flowing, NMEA position sentences are ignored.
    CHECK(!rx.feed("$GNRMC,025650.00,A,3302.68922,N,09659.30219,W,0.032,,151126,,,A*76\r\n"));
    CHECK(!rx.feed("$GNGGA,025650.00,3302.68922,N,09659.30219,W,1,12,0.83,142.3,M,-25.2,M,,*7F\r\n"));
    CHECK(reports.size() == 3);
    NEAR(*rx.state().lat, 33.0448126, 1e-9);

    // A NAK and unrelated frames are harmless.
    rx.feedBytes(ubx::encode(ubx::kClassAck, ubx::kAckNak, std::string("\x06\x01", 2)));
    rx.feedBytes(ubx::encode(0x0a, 0x04, "ver"));
    CHECK(reports.size() == 3);

    // The module loses its fix.
    std::string noFix = kRealNavPvt.substr(6, kRealNavPvt.size() - 8);
    noFix[20] = 0;  // fixType none
    noFix[21] = 0;  // gnssFixOK cleared
    putU32(noFix, 0, 15558000);
    rx.feedBytes(ubx::encode(ubx::kClassNav, ubx::kNavPvt, noFix));
    rx.feedBytes(atItow(kRealNavDop, 15558000));
    rx.feedBytes(navSat(15558000, 5, 0));
    CHECK(reports.size() == 5);  // the incomplete 15557000 epoch is flushed first, then this one completes
    CHECK(reports.back().mode == 1 && !rx.state().ownship() && !reports.back().eph);
}

static void testMaxM8qFallsBackToNmea()
{
    EventLoop loop;
    MaxM8qReceiver rx(loop, "/dev/null");
    int fixes = 0;
    rx.onFix = [&](const GnssState &) { ++fixes; };
    const char *rmc = "$GNRMC,025650.00,A,3302.68922,N,09659.30219,W,0.032,,151126,,,A*76\r\n";
    const char *gga = "$GNGGA,025650.00,3302.68922,N,09659.30219,W,1,12,0.83,142.3,M,-25.2,M,,*7F\r\n";

    // Never saw UBX (TX wire not connected, say): NMEA is the fix from the first epoch.
    rx.feed(rmc);
    CHECK(rx.feed(gga) && fixes == 1);

    // UBX arrives and takes over...
    rx.feedBytes(kRealNavPvt);
    rx.feedBytes(kRealNavDop);
    rx.feedBytes(navSat(15554000, 25, 16));
    CHECK(fixes == 2 && rx.state().eph);
    for (int epoch = 0; epoch < 2; ++epoch)
    {
        rx.feed(rmc);
        CHECK(!rx.feed(gga));  // within the grace period
    }
    CHECK(fixes == 2);

    // ...then goes silent (the module reset): after the grace period NMEA is trusted again and the UBX-only
    // fields stop being reported.
    rx.feed(rmc);
    CHECK(rx.feed(gga) && fixes == 3);
    NEAR(*rx.state().lat, 33.0 + 2.68922 / 60.0, 1e-9);
    CHECK(!rx.state().eph && !rx.state().climbFpm);

    // The startup commands switch on exactly the UBX messages we read and switch off the redundant NMEA ones.
    std::vector<std::string> cmds;
    StreamSplitter split;
    split.onUbx = [&](const ubx::Frame &f)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02x-%02x=%d", static_cast<int>(f.payload[0]), static_cast<int>(f.payload[1]),
                      static_cast<int>(f.payload[2]));
        CHECK(f.cls == ubx::kClassCfg && f.id == ubx::kCfgMsg && f.payload.size() == 3);
        cmds.push_back(buf);
    };
    split.feed(MaxM8qReceiver::configurationCommands());
    const std::vector<std::string> want = {"01-07=1", "01-04=1", "01-35=1", "f0-01=0", "f0-02=0", "f0-03=0", "f0-05=0"};
    CHECK(cmds == want);  // GGA (f0-00) and RMC (f0-04) stay on as the fallback
}

static void testConfig() {
    const Config d = Config::fromYaml("");
    CHECK(d.port == 5555 && d.debugPort == 5556 && !d.gnssEnabled && !d.fallbackLat);

    const Config c = Config::fromYaml(
        "server: {port: 6000, debug_port: null}\n"
        "gnss: {enabled: true}\n"
        "ownship_fallback: {lat: 1.5, lon: 2.5}\n"
        "log_level: debug\n");
    CHECK(c.port == 6000 && !c.debugPort && c.gnssEnabled && *c.fallbackLat == 1.5 && c.logLevel == "debug");

    CHECK(d.gnssReceiver == "max-m8q" && d.gnssDevice == "/dev/ttyAMA0" && !d.gnssBaud);
    const Config s = Config::fromYaml("gnss: {enabled: true, receiver: gt-u7, device: /dev/ttyUSB0, baud: 38400}");
    CHECK(s.gnssReceiver == "gt-u7" && s.gnssDevice == "/dev/ttyUSB0" && s.gnssBaud == 38400);
    CHECK_THROWS(Config::fromYaml("gnss: {receiver: usb}"));
    CHECK_THROWS(Config::fromYaml("gnss: {source: serial}"));  // replaced by "receiver"
    CHECK_THROWS(Config::fromYaml("gnss: {baud: 1234}"));

    CHECK_THROWS(Config::fromYaml("sever: {}"));                          // typo'd key
    CHECK_THROWS(Config::fromYaml("server: {prot: 1}"));
    CHECK_THROWS(Config::fromYaml("server: {port: 99999}"));
    CHECK_THROWS(Config::fromYaml("server: {port: 5555, debug_port: 5555}"));
    CHECK_THROWS(Config::fromYaml("server: {port: abc}"));
    CHECK_THROWS(Config::fromYaml("ownship_fallback: {lat: 1}"));
    CHECK_THROWS(Config::fromYaml("traffic_hz: 0"));
    CHECK_THROWS(Config::fromYaml("log_level: loud"));
    CHECK_THROWS(Config::fromFile("/nonexistent/dolo.yaml"));
}

// ---- Hub integration: a blocking client on a thread talks to the hub running on the main thread ----

static int connectTo(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { close(fd); return -1; }
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

static bool readExactly(int fd, void* p, size_t n) {
    auto* b = static_cast<char*>(p);
    while (n) {
        const ssize_t r = recv(fd, b, n, 0);
        if (r <= 0) return false;
        b += r; n -= static_cast<size_t>(r);
    }
    return true;
}

static std::optional<pb::Envelope> readEnvelope(int fd) {
    unsigned char hdr[4];
    if (!readExactly(fd, hdr, 4)) return std::nullopt;
    std::string body((hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3], '\0');
    if (!readExactly(fd, body.data(), body.size())) return std::nullopt;
    pb::Envelope env;
    if (!env.ParseFromString(body)) return std::nullopt;
    return env;
}

static void sendEnvelope(int fd, const pb::Envelope& env) {
    const std::string f = encodeFrame(env.SerializeAsString());
    send(fd, f.data(), f.size(), 0);
}

static pb::Envelope helloWith(uint32_t version) {
    auto env = makeEnvelope();
    env.mutable_client_hello()->set_protocol_version(version);
    env.mutable_client_hello()->set_app_version("test");
    return env;
}

static void testHub() {
    EventLoop loop;
    Hub hub(loop, {pb::CAPABILITY_TRAFFIC});
    constexpr int kPort = 45555, kDebug = 45556;
    hub.listen("127.0.0.1", kPort);
    hub.listenDebug("127.0.0.1", kDebug);

    int counter = 0;
    loop.every(0.05, [&] {
        auto env = makeEnvelope();
        env.mutable_traffic()->set_messages_total(++counter);
        hub.broadcast(env);
    });

    std::thread client([&] {
        // Wrong protocol version: closed without a ServerHello.
        int bad = connectTo(kPort);
        CHECK(bad >= 0);
        sendEnvelope(bad, helloWith(99));
        CHECK(!readEnvelope(bad));
        close(bad);

        // Oversized frame: closed.
        bad = connectTo(kPort);
        const unsigned char huge[4] = {0x00, 0x10, 0x00, 0x01};
        send(bad, huge, 4, 0);
        CHECK(!readEnvelope(bad));
        close(bad);

        const int dbg = connectTo(kDebug);
        const int fd = connectTo(kPort);
        CHECK(fd >= 0 && dbg >= 0);
        sendEnvelope(fd, helloWith(1));
        auto hello = readEnvelope(fd);
        CHECK(hello && hello->payload_case() == pb::Envelope::kServerHello);
        CHECK(hello && hello->server_hello().protocol_version() == 1 && hello->server_hello().capabilities_size() == 1);

        auto ping = makeEnvelope();
        ping.mutable_ping()->set_seq(42);
        sendEnvelope(fd, ping);
        bool gotPong = false, gotTraffic = false;
        for (int i = 0; i < 20 && !(gotPong && gotTraffic); ++i) {
            auto env = readEnvelope(fd);
            if (!env) break;
            gotPong |= env->payload_case() == pb::Envelope::kPong && env->pong().seq() == 42;
            gotTraffic |= env->payload_case() == pb::Envelope::kTraffic;
        }
        CHECK(gotPong && gotTraffic);

        char buf[512] = {};
        std::string seen;
        while (seen.find("\"traffic\"") == std::string::npos) {
            const ssize_t n = recv(dbg, buf, sizeof buf - 1, 0);
            if (n <= 0) break;
            seen.append(buf, static_cast<size_t>(n));
        }
        CHECK(seen.find("\"serverHello\"") != std::string::npos);
        CHECK(seen.find("\"messagesTotal\"") != std::string::npos);
        CHECK(seen.find('\n') != std::string::npos);
        close(fd);
        close(dbg);
        loop.stop();
    });
    loop.run();
    client.join();
}

int main() {
    struct { const char* name; void (*fn)(); } tests[] = {
        {"framing", testFraming}, {"geo", testGeo}, {"sbs+snapshot", testSbsAndSnapshot},
        {"gpsd", testGpsd}, {"nmea", testNmeaParse}, {"gt-u7", testGtU7}, {"max-m8q", testMaxM8q},
        {"ubx", testUbx}, {"stream", testStreamSplitter}, {"max-m8q-ubx", testMaxM8qUbx},
        {"max-m8q-fallback", testMaxM8qFallsBackToNmea}, {"config", testConfig}, {"hub", testHub},
    };
    for (const auto& t : tests) {
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    return g_failures ? 1 : 0;
}
