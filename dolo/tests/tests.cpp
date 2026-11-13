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
#include "gnss.hpp"
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

static void testGnss() {
    GnssState g;
    CHECK(!g.ownship() && g.toProto().fix_mode() == pb::FIX_MODE_NO_FIX);
    g.apply(nlohmann::json::parse(R"({"class":"SKY","hdop":0.9,"satellites":[{"used":true},{"used":false}]})"));
    const bool tpv = g.apply(nlohmann::json::parse(
        R"({"class":"TPV","mode":3,"time":"2026-01-01T00:00:00.500Z","lat":40.0,"lon":-75.0,"altMSL":100.0,"speed":10.0,"eph":3.0})"));
    CHECK(tpv);
    const auto f = g.toProto();
    CHECK(f.fix_mode() == pb::FIX_MODE_3D && f.satellites_used() == 1 && f.satellites_visible() == 2);
    CHECK(f.fix_unix_ms() == 1767225600500ULL);
    NEAR(f.ground_speed_kt(), 19.44, 0.01);
    NEAR(*g.ownship()->altMslFt, 328.084, 1e-3);
    g.apply(nlohmann::json::parse(R"({"class":"TPV","mode":1})"));
    CHECK(!g.ownship());
}

static std::string nmea(const std::string& body) {
    unsigned sum = 0;
    for (const char c : body) sum ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return "$" + body + tail;
}

static void testNmea() {
    GnssState g;
    // Real cold-start output from the GT-U7: no fix yet.
    CHECK(!g.applyNmea("$GPRMC,,V,,,,,,,,,,N*53\r\n"));
    CHECK(g.applyNmea("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n"));
    CHECK(!g.ownship() && g.toProto().fix_mode() == pb::FIX_MODE_NO_FIX && g.used == 0);

    // A fix: 40 deg 30.000' N, 75 deg 15.000' W, 100.0 m MSL, 10 kt, track 90.
    CHECK(!g.applyNmea(nmea("GPGSV,1,1,02,01,40,083,46,02,17,308,41")));
    CHECK(!g.applyNmea(nmea("GLGSV,1,1,03,65,10,100,30,66,20,200,31,67,30,300,32")));
    CHECK(!g.applyNmea(nmea("GPRMC,010203.50,A,4030.000,N,07515.000,W,10.0,90.0,010126,,,A")));
    CHECK(!g.applyNmea(nmea("GPGSA,A,3,01,02,,,,,,,,,,,1.8,0.9,1.5")));
    CHECK(g.applyNmea(nmea("GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")));
    NEAR(*g.lat, 40.5, 1e-9);
    NEAR(*g.lon, -75.25, 1e-9);
    const auto f = g.toProto();
    CHECK(f.fix_mode() == pb::FIX_MODE_3D && f.satellites_used() == 7 && f.satellites_visible() == 5);
    CHECK(f.fix_unix_ms() == 1767229323500ULL);  // 2026-01-01T01:02:03.500Z
    NEAR(f.altitude_msl_m(), 100.0, 1e-3);
    NEAR(f.ground_speed_kt(), 10.0, 1e-3);
    NEAR(f.track_true_deg(), 90.0, 1e-3);
    NEAR(f.hdop(), 0.9, 1e-3);
    NEAR(f.vdop(), 1.5, 1e-3);
    CHECK(g.ownship() && g.ownship()->altMslFt);

    // A 2D fix from GSA wins over GGA's altitude.
    g.applyNmea(nmea("GNGSA,A,2,01,02,,,,,,,,,,,1.8,0.9,1.5"));
    g.applyNmea(nmea("GNGGA,010204.00,4030.000,N,07515.000,W,1,04,0.9,100.0,M,-34.0,M,,"));
    CHECK(g.toProto().fix_mode() == pb::FIX_MODE_2D && !g.toProto().has_altitude_msl_m());

    // Bad input is ignored.
    GnssState b;
    CHECK(!b.applyNmea("$GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,*00"));  // bad checksum
    CHECK(!b.applyNmea("GPGGA,no dollar sign"));
    CHECK(!b.applyNmea(nmea("GPGGA,010203.50,9900.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,")) || !b.ownship());
    CHECK(!b.ownship());
    CHECK(!b.applyNmea(""));

    // Losing the fix clears the position again.
    CHECK(g.applyNmea("$GPGGA,,,,,,0,00,99.99,,,,,,*48\r\n") && !g.ownship());
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

    CHECK(d.gnssSource == "gpsd" && d.gnssDevice == "/dev/ttyAMA0" && d.gnssBaud == 9600);
    const Config s = Config::fromYaml("gnss: {enabled: true, source: serial, device: /dev/ttyUSB0, baud: 38400}");
    CHECK(s.gnssSource == "serial" && s.gnssDevice == "/dev/ttyUSB0" && s.gnssBaud == 38400);
    CHECK_THROWS(Config::fromYaml("gnss: {source: usb}"));
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
        {"gnss", testGnss}, {"nmea", testNmea}, {"config", testConfig}, {"hub", testHub},
    };
    for (const auto& t : tests) {
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    return g_failures ? 1 : 0;
}
