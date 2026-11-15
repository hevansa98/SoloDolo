#include "gnss/gt_u7.hpp"

namespace dolo
{

    GtU7Receiver::GtU7Receiver(EventLoop &loop, std::string device, std::optional<int> baud)
        : UartGnssReceiver(loop, "GT-U7", std::move(device), baud.value_or(kDefaultBaud)) {}

    void GtU7Receiver::onGsa(const nmea::Gsa &gsa)
    {
        fixType_ = gsa.fixType;
        state_.vdop = gsa.vdop;
    }

    void GtU7Receiver::onGsv(const nmea::Gsv &gsv)
    {
        state_.visible = gsv.inView; // every GSV message repeats the total in view
    }

} // namespace dolo
