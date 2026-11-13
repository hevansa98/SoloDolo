#include "config.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace dolo {
namespace {

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("config: " + msg); }

YAML::Node section(const YAML::Node& root, const char* name, const std::set<std::string>& keys) {
    const YAML::Node node = root[name];
    if (!node) return node;
    if (!node.IsMap()) fail(std::string(name) + " must be a mapping");
    for (const auto& kv : node) {
        if (!keys.count(kv.first.as<std::string>())) fail("unknown key " + std::string(name) + "." + kv.first.as<std::string>());
    }
    return node;
}

template <typename T>
void read(const YAML::Node& sec, const char* key, T& out) {
    if (!sec) return;  // section absent: keep defaults
    const YAML::Node n = sec[key];
    if (!n) return;
    try {
        out = n.as<T>();
    } catch (const YAML::Exception&) {
        fail(std::string("invalid value for ") + key);
    }
}

void requirePort(int p, const char* name) {
    if (p < 1 || p > 65535) fail(std::string(name) + " must be 1-65535");
}

}  // namespace

Config Config::fromYaml(const std::string& text) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        fail(e.what());
    }
    Config c;
    if (root.IsNull()) return c;  // empty file: all defaults
    if (!root.IsMap()) fail("top level must be a mapping");
    static const std::set<std::string> top = {"server", "adsb", "gnss", "traffic_hz", "ownship_fallback", "log_level"};
    for (const auto& kv : root) {
        if (!top.count(kv.first.as<std::string>())) fail("unknown key " + kv.first.as<std::string>());
    }

    const auto server = section(root, "server", {"listen_host", "port", "debug_port"});
    read(server, "listen_host", c.listenHost);
    read(server, "port", c.port);
    if (server && server["debug_port"]) {
        if (server["debug_port"].IsNull()) {
            c.debugPort.reset();
        } else {
            int p = 0;
            read(server, "debug_port", p);
            c.debugPort = p;
        }
    }

    const auto adsb = section(root, "adsb", {"host", "port", "prune_after_s"});
    read(adsb, "host", c.sbsHost);
    read(adsb, "port", c.sbsPort);
    read(adsb, "prune_after_s", c.pruneAfterS);

    const auto gnss = section(root, "gnss", {"enabled", "host", "port"});
    read(gnss, "enabled", c.gnssEnabled);
    read(gnss, "host", c.gpsdHost);
    read(gnss, "port", c.gpsdPort);

    read(root, "traffic_hz", c.trafficHz);
    read(root, "log_level", c.logLevel);

    const auto fb = section(root, "ownship_fallback", {"lat", "lon"});
    if (fb) {
        double lat = 0, lon = 0;
        if (!fb["lat"] || !fb["lon"]) fail("ownship_fallback needs both lat and lon");
        read(fb, "lat", lat);
        read(fb, "lon", lon);
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) fail("ownship_fallback out of range");
        c.fallbackLat = lat;
        c.fallbackLon = lon;
    }

    requirePort(c.port, "server.port");
    if (c.debugPort) requirePort(*c.debugPort, "server.debug_port");
    if (c.debugPort && *c.debugPort == c.port) fail("server.debug_port must differ from server.port");
    requirePort(c.sbsPort, "adsb.port");
    requirePort(c.gpsdPort, "gnss.port");
    if (c.trafficHz <= 0 || c.trafficHz > 20) fail("traffic_hz must be in (0, 20]");
    if (c.pruneAfterS <= 0) fail("adsb.prune_after_s must be positive");
    if (c.logLevel != "debug" && c.logLevel != "info" && c.logLevel != "warning" && c.logLevel != "error")
        fail("log_level must be debug, info, warning or error");
    return c;
}

Config Config::fromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) fail("cannot read " + path);
    std::ostringstream text;
    text << in.rdbuf();
    return fromYaml(text.str());
}

}  // namespace dolo
