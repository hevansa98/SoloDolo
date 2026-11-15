#include "gnss/factory.hpp"

#include <stdexcept>

#include "config.hpp"
#include "gnss/gpsd.hpp"
#include "gnss/gt_u7.hpp"
#include "gnss/max_m8q.hpp"

namespace dolo
{

    bool isKnownGnssReceiver(const std::string &name) { return name == "gt-u7" || name == "max-m8q" || name == "gpsd"; }

    std::unique_ptr<GnssReceiver> makeGnssReceiver(const Config &cfg, EventLoop &loop)
    {
        if (cfg.gnssReceiver == "gt-u7")
            return std::make_unique<GtU7Receiver>(loop, cfg.gnssDevice, cfg.gnssBaud);
        if (cfg.gnssReceiver == "max-m8q")
            return std::make_unique<MaxM8qReceiver>(loop, cfg.gnssDevice, cfg.gnssBaud);
        if (cfg.gnssReceiver == "gpsd")
            return std::make_unique<GpsdReceiver>(loop, cfg.gpsdHost, cfg.gpsdPort);
        throw std::runtime_error("unknown gnss receiver " + cfg.gnssReceiver);
    }

} // namespace dolo
