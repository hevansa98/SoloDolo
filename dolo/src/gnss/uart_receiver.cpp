#include "gnss/uart_receiver.hpp"

namespace dolo
{

    UartGnssReceiver::UartGnssReceiver(EventLoop &loop, std::string name, std::string device, int baud)
        : port_(loop, std::move(name), std::move(device), baud)
    {
        port_.onOpen = [this] { onOpen(); };
        port_.onData = [this](std::string_view bytes) { feedBytes(bytes); };
        splitter_.onNmea = [this](std::string_view line) { feed(line); };
        splitter_.onUbx = [this](const ubx::Frame &frame) { onUbx(frame); };
    }

    bool UartGnssReceiver::feed(std::string_view line)
    {
        const auto sentence = nmea::parse(line);
        if (!sentence)
            return false;

        if (const auto *rmc = std::get_if<nmea::Rmc>(&*sentence))
        {
            onEpochStart();
            if (!nmeaFixWanted())
                return false;
            if (rmc->timeMs)
                state_.timeMs = *rmc->timeMs;
            state_.speedKt = rmc->speedKt;
            state_.track = rmc->trackDeg;
        }
        else if (const auto *gga = std::get_if<nmea::Gga>(&*sentence))
        {
            if (!nmeaFixWanted())
                return false;
            state_.used = gga->satsUsed;
            state_.hdop = gga->hdop;
            if (gga->quality > 0 && gga->lat && gga->lon)
            {
                state_.lat = gga->lat;
                state_.lon = gga->lon;
                state_.altM = gga->altMslM;
                state_.mode = fixType_ >= 2 ? fixType_ : (state_.altM ? 3 : 2);
            }
            else
            {
                state_.mode = 1;
                state_.lat = state_.lon = state_.altM = std::nullopt;
                state_.speedKt = state_.track = state_.climbFpm = std::nullopt;
            }
            publish();
            return true;
        }
        else if (const auto *gsa = std::get_if<nmea::Gsa>(&*sentence))
        {
            onGsa(*gsa);
        }
        else if (const auto *gsv = std::get_if<nmea::Gsv>(&*sentence))
        {
            onGsv(*gsv);
        }
        return false;
    }

} // namespace dolo
