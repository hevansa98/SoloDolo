// MAX-M8Q: u-blox M8 module tracking several constellations at once (GPS + GLONASS by default).
//
// Primary source is the UBX binary protocol: NAV-PVT, NAV-DOP and NAV-SAT carry everything NMEA does, plus
// horizontal accuracy and vertical speed. Dolo switches those messages on at startup. If the module stops
// sending them (it reset and forgot, or the TX wire is not connected), the receiver falls back to NMEA, where
// each epoch carries one GSA and one GSV group per constellation that have to be combined.
#pragma once

#include <map>
#include <optional>

#include "gnss/uart_receiver.hpp"

namespace dolo
{

    class MaxM8qReceiver : public UartGnssReceiver
    {
    public:
        static constexpr int kDefaultBaud = 9600;

        MaxM8qReceiver(EventLoop &loop, std::string device, std::optional<int> baud = std::nullopt);

        std::string model() const override { return "u-blox MAX-M8Q"; }

        /// The UBX-CFG-MSG commands sent when the port opens and again whenever UBX goes quiet. They switch on
        /// NAV-PVT, NAV-DOP and NAV-SAT and switch off the NMEA sentences UBX makes redundant. At 9600 baud
        /// (about 960 bytes/s) the full NMEA set plus the UBX messages would not fit and the module would drop
        /// messages. RMC and GGA stay on as the fallback. Nothing is saved to the module's flash, so a power
        /// cycle restores its defaults.
        static std::string configurationCommands();

    protected:
        void onOpen() override;
        void onEpochStart() override;
        bool nmeaFixWanted() const override;
        void onGsa(const nmea::Gsa &gsa) override;
        void onGsv(const nmea::Gsv &gsv) override;
        void onUbx(const ubx::Frame &frame) override;

    private:
        // Number of NMEA epochs (RMC sentences) without a NAV-PVT before the NMEA fix is trusted instead, and how
        // often the commands are resent while UBX stays silent.
        static constexpr unsigned kUbxGraceEpochs = 2, kReconfigureEveryEpochs = 10;

        void applyPvt(const ubx::NavPvt &pvt);
        void joinEpoch(uint32_t iTow);
        void publishIfComplete();

        // UBX. The messages of one epoch share an iTOW but arrive in any order, so a report goes out once all
        // three are in, or when the next epoch starts if one never came.
        unsigned silentEpochs_ = kUbxGraceEpochs + 1; // NMEA epochs since the last NAV-PVT; starts as "never seen"
        uint32_t epochItow_ = 0;
        bool havePvt_ = false, haveDop_ = false, haveSat_ = false, published_ = false;

        // NMEA fallback. The previous epoch's values stay in place until the first sentence of the new epoch
        // replaces them, so a report published at GGA (before this epoch's GSA/GSV arrive) never sees a
        // half-built picture.
        bool gsaFresh_ = true, gsvFresh_ = true;
        std::map<std::string, uint32_t> viewByTalker_;
    };

} // namespace dolo
