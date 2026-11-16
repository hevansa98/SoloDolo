#include "gnss/factory.hpp"

#include <stdexcept>

#include "gnss/gpsd.hpp"
#include "gnss/gt_u7.hpp"
#include "gnss/max_m8q.hpp"

namespace dolo
{

    bool isKnownGnssReceiver(const std::string &name) { return name == "gt-u7" || name == "max-m8q" || name == "gpsd"; }

    std::unique_ptr<GnssReceiver> makeGnssReceiver(const GnssSettings &settings, EventLoop &loop)
    {
        if (settings.receiver == "gt-u7")
            return std::make_unique<GtU7Receiver>(loop, settings.device, settings.baud);
        if (settings.receiver == "max-m8q")
            return std::make_unique<MaxM8qReceiver>(loop, settings.device, settings.baud);
        if (settings.receiver == "gpsd")
            return std::make_unique<GpsdReceiver>(loop, settings.gpsdHost, settings.gpsdPort);
        throw std::runtime_error("unknown gnss receiver " + settings.receiver);
    }

} // namespace dolo
