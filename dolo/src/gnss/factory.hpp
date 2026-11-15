// Maps the gnss.receiver config value to a GnssReceiver subclass. Adding a receiver model means writing its
// class and adding one line to each of the two functions in factory.cpp.
#pragma once

#include <memory>
#include <string>

#include "gnss/receiver.hpp"

namespace dolo
{

    struct Config;
    class EventLoop;

    /// "gt-u7", "max-m8q" or "gpsd".
    bool isKnownGnssReceiver(const std::string &name);

    /// Builds the receiver selected by cfg.gnssReceiver. Throws std::runtime_error for an unknown name.
    std::unique_ptr<GnssReceiver> makeGnssReceiver(const Config &cfg, EventLoop &loop);

} // namespace dolo
