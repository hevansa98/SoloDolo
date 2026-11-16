// Base for GNSS modules on a UART. It owns the serial port, splits the byte stream into NMEA sentences and
// vendor binary frames, and holds the NMEA-to-state mapping every module shares. What differs per module (how the
// fix type and satellites in view are assembled, and any vendor protocol on top) is left to the subclass.
#pragma once

#include <string>
#include <string_view>

#include "core/event_loop.hpp"
#include "gnss/nmea.hpp"
#include "gnss/receiver.hpp"
#include "gnss/stream.hpp"
#include "gnss/ubx.hpp"
#include "io/serial.hpp"

namespace dolo
{

    class UartGnssReceiver : public GnssReceiver
    {
    public:
        void start() override { port_.start(); }
        const GnssState &state() const override { return state_; }

        /// Processes one NMEA sentence. Returns true if it completed a position report (and called onFix).
        bool feed(std::string_view line);

        /// Processes raw bytes exactly as read from the port (NMEA and binary frames, any chunking).
        /// The serial port calls this; tests call it directly.
        void feedBytes(std::string_view bytes) { splitter_.feed(bytes); }

    protected:
        UartGnssReceiver(EventLoop &loop, std::string name, std::string device, int baud);

        /// Called each time the serial port opens; a module that needs commands sent at startup does so here.
        virtual void onOpen() {}
        /// Called when a new reporting epoch begins (the RMC sentence opens each epoch on u-blox modules).
        virtual void onEpochStart() {}
        /// False while another source (e.g. a vendor binary protocol) supplies the fix; RMC and GGA are then ignored.
        virtual bool nmeaFixWanted() const { return true; }
        virtual void onGsa(const nmea::Gsa &gsa) = 0;
        virtual void onGsv(const nmea::Gsv &gsv) = 0;
        /// A UBX frame arrived. Modules that do not speak UBX never see one.
        virtual void onUbx(const ubx::Frame &) {}

        bool write(std::string_view bytes) { return port_.write(bytes); }
        void publish()
        {
            if (onFix)
                onFix(state_);
        }

        GnssState state_;
        int fixType_ = 0; // latest fix type from GSA: 0 unknown, 1 none, 2 2D, 3 3D

    private:
        SerialPort port_;
        StreamSplitter splitter_;
    };

} // namespace dolo
