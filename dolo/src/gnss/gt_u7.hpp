// GT-U7: u-blox 7 module, GPS only. Emits one GSA and one set of GSV sentences per epoch, all GP-talker.
#pragma once

#include <optional>

#include "gnss/uart_receiver.hpp"

namespace dolo
{

    class GtU7Receiver : public UartGnssReceiver
    {
    public:
        static constexpr int kDefaultBaud = 9600;

        GtU7Receiver(EventLoop &loop, std::string device, std::optional<int> baud = std::nullopt);

        std::string model() const override { return "u-blox GT-U7"; }

    protected:
        void onGsa(const nmea::Gsa &gsa) override;
        void onGsv(const nmea::Gsv &gsv) override;
    };

} // namespace dolo
