// Dolo: Raspberry Pi ADS-B/GNSS bridge for the Solo iPad app.
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "aircraft.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "event_loop.hpp"
#include "gnss.hpp"
#include "hub.hpp"
#include "log.hpp"
#include "net.hpp"
#include "sbs.hpp"
#include "serial.hpp"

namespace {

constexpr const char* kDefaultConfigPath = "/etc/dolo/dolo.yaml";

void usage() {
    std::puts("usage: dolo [-c|--config FILE] [--version]\n"
              "  Without --config, /etc/dolo/dolo.yaml is used if present, otherwise built-in defaults.");
}

dolo::LogLevel parseLevel(const std::string& s) {
    if (s == "debug") return dolo::LogLevel::Debug;
    if (s == "warning") return dolo::LogLevel::Warning;
    if (s == "error") return dolo::LogLevel::Error;
    return dolo::LogLevel::Info;
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--version") {
            std::puts("dolo " DOLO_VERSION);
            return 0;
        } else {
            usage();
            return arg == "-h" || arg == "--help" ? 0 : 2;
        }
    }

    try {
        struct stat st;
        if (configPath.empty() && stat(kDefaultConfigPath, &st) == 0) configPath = kDefaultConfigPath;
        const dolo::Config cfg = configPath.empty() ? dolo::Config{} : dolo::Config::fromFile(configPath);
        dolo::logThreshold() = parseLevel(cfg.logLevel);
        if (configPath.empty()) LOG_INFO("no config file; using defaults");
        else LOG_INFO("config: %s", configPath.c_str());

        dolo::EventLoop::installSignalHandlers();
        dolo::EventLoop loop;
        dolo::AircraftStore store(cfg.pruneAfterS);
        dolo::GnssState gnss;

        using solodolo::v1::Capability;
        std::vector<Capability> caps = {solodolo::v1::CAPABILITY_TRAFFIC};
        if (cfg.gnssEnabled) caps.push_back(solodolo::v1::CAPABILITY_GNSS);
        dolo::Hub hub(loop, caps);
        hub.listen(cfg.listenHost, cfg.port);
        if (cfg.debugPort) hub.listenDebug(cfg.listenHost, *cfg.debugPort);

        dolo::TcpClient sbs(loop, "SBS", cfg.sbsHost, cfg.sbsPort);
        sbs.onLine = [&](std::string_view line) {
            if (const auto msg = dolo::parseSbsLine(line)) store.update(msg->icao, msg->fields, dolo::monotonicS());
        };
        sbs.start();

        auto publishGnss = [&] {
            auto env = dolo::makeEnvelope();
            *env.mutable_gnss() = gnss.toProto();
            hub.broadcast(env);
        };

        std::unique_ptr<dolo::TcpClient> gpsd;
        std::unique_ptr<dolo::SerialLineReader> nmea;
        if (cfg.gnssEnabled && cfg.gnssSource == "serial") {
            nmea = std::make_unique<dolo::SerialLineReader>(loop, "GNSS", cfg.gnssDevice, cfg.gnssBaud);
            nmea->onLine = [&](std::string_view line) {
                if (gnss.applyNmea(line)) publishGnss();
            };
            nmea->start();
        } else if (cfg.gnssEnabled) {
            gpsd = std::make_unique<dolo::TcpClient>(loop, "gpsd", cfg.gpsdHost, cfg.gpsdPort);
            dolo::TcpClient* g = gpsd.get();
            g->onConnect = [g] { g->send("?WATCH={\"enable\":true,\"json\":true}\n"); };
            g->onLine = [&](std::string_view line) {
                const auto j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
                if (!j.is_discarded() && gnss.apply(j)) publishGnss();
            };
            g->start();
        } else {
            LOG_INFO("GNSS disabled; range/bearing %s", cfg.fallbackLat ? "use ownship_fallback" : "unavailable (set ownship_fallback)");
        }

        loop.every(1.0 / cfg.trafficHz, [&] {
            std::optional<dolo::Ownship> own = gnss.ownship();
            if (!own && cfg.fallbackLat) own = dolo::Ownship{*cfg.fallbackLat, *cfg.fallbackLon, std::nullopt};
            auto env = dolo::makeEnvelope();
            *env.mutable_traffic() = store.snapshot(own, dolo::monotonicS());
            hub.broadcast(env);
        });

        LOG_INFO("Dolo %s listening on %s:%d (debug %s)", DOLO_VERSION, cfg.listenHost.c_str(), cfg.port,
                 cfg.debugPort ? std::to_string(*cfg.debugPort).c_str() : "off");
        loop.run();
        LOG_INFO("shutting down");
    } catch (const std::exception& e) {
        LOG_ERROR("%s", e.what());
        return 1;
    }
    return 0;
}
