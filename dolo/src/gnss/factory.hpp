// Maps the gnss.receiver config value to a GnssReceiver subclass. Adding a receiver model means writing its
// class and adding one line to each of the two functions in factory.cpp.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "gnss/receiver.hpp"

namespace dolo
{

    class EventLoop;

    /// Everything needed to build a receiver. The GNSS code knows nothing about the app's config file; the app
    /// fills this in.
    struct GnssSettings
    {
        std::string receiver = "max-m8q";  // "gt-u7", "max-m8q" or "gpsd"
        std::string device = "/dev/ttyAMA0"; // serial receivers
        std::optional<int> baud;             // serial receivers; unset uses the receiver's own default
        std::string gpsdHost = "127.0.0.1";  // gpsd
        int gpsdPort = 2947;
    };

    /// "gt-u7", "max-m8q" or "gpsd".
    bool isKnownGnssReceiver(const std::string &name);

    /// Builds the receiver selected by settings.receiver. Throws std::runtime_error for an unknown name.
    std::unique_ptr<GnssReceiver> makeGnssReceiver(const GnssSettings &settings, EventLoop &loop);

} // namespace dolo
