// The one thing the rest of Dolo knows about GNSS hardware: something that produces GnssState.
// Each receiver model (or source such as gpsd) is a subclass; main.cpp talks only to this interface.
#pragma once

#include <functional>
#include <string>

#include "gnss/state.hpp"

namespace dolo
{

    class GnssReceiver
    {
    public:
        virtual ~GnssReceiver() = default;

        virtual void start() = 0;
        virtual std::string model() const = 0; // for logs, e.g. "u-blox MAX-M8Q"
        virtual const GnssState &state() const = 0;

        /// Called after every position report with the merged state.
        std::function<void(const GnssState &)> onFix;
    };

} // namespace dolo
