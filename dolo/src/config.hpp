// Runtime configuration, loaded from a YAML file (see config/dolo.yaml).
#pragma once

#include <optional>
#include <string>

namespace dolo {

struct Config {
    // server
    std::string listenHost = "0.0.0.0";
    int port = 5555;                  // protobuf, used by the iPad app
    std::optional<int> debugPort = 5556;  // NDJSON mirror of the stream; nullopt disables

    // adsb: dump1090's SBS output
    std::string sbsHost = "127.0.0.1";
    int sbsPort = 30003;
    double pruneAfterS = 60.0;

    // gnss: opt-in until a GNSS receiver is fitted
    bool gnssEnabled = false;
    std::string gnssSource = "gpsd";  // "gpsd" or "serial" (raw NMEA read straight from gnssDevice)
    std::string gnssDevice = "/dev/ttyAMA0";
    int gnssBaud = 9600;
    std::string gpsdHost = "127.0.0.1";
    int gpsdPort = 2947;

    double trafficHz = 1.0;

    // Receiver position used for range/bearing when there is no GNSS fix
    std::optional<double> fallbackLat, fallbackLon;

    std::string logLevel = "info";

    /// Parses YAML text. Throws std::runtime_error on bad syntax, unknown keys or invalid values.
    static Config fromYaml(const std::string& text);
    static Config fromFile(const std::string& path);
};

}  // namespace dolo
