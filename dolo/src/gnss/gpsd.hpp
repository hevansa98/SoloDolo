// GNSS from a running gpsd (TPV + SKY JSON). Any receiver gpsd supports can sit behind it.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "gnss/receiver.hpp"
#include "net.hpp"

namespace dolo
{

    class GpsdReceiver : public GnssReceiver
    {
    public:
        GpsdReceiver(EventLoop &loop, std::string host, int port);

        void start() override { client_.start(); }
        std::string model() const override { return "gpsd"; }
        const GnssState &state() const override { return state_; }

        /// Applies one gpsd message. Returns true for a TPV (position) report.
        bool apply(const nlohmann::json &msg);

    private:
        GnssState state_;
        TcpClient client_;
    };

} // namespace dolo
