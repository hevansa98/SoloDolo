#include "gnss/gpsd.hpp"

#include <ctime>

#include "gnss/units.hpp"

namespace dolo
{
    namespace
    {

        std::optional<double> num(const nlohmann::json &j, const char *key)
        {
            const auto it = j.find(key);
            if (it == j.end() || !it->is_number())
                return std::nullopt;
            return it->get<double>();
        }

        /// "2026-01-01T00:00:00.500Z" -> Unix ms.
        std::optional<uint64_t> parseIsoMs(const std::string &s)
        {
            std::tm tm{};
            const char *rest = strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
            if (!rest)
                return std::nullopt;
            uint64_t ms = static_cast<uint64_t>(timegm(&tm)) * 1000;
            if (*rest == '.')
            {
                int digits = 0, frac = 0;
                for (++rest; *rest >= '0' && *rest <= '9' && digits < 3; ++rest, ++digits)
                    frac = frac * 10 + (*rest - '0');
                while (digits++ < 3)
                    frac *= 10;
                ms += frac;
            }
            return ms;
        }

    } // namespace

    GpsdReceiver::GpsdReceiver(EventLoop &loop, std::string host, int port) : client_(loop, "gpsd", std::move(host), port)
    {
        client_.onConnect = [this]
        { client_.send("?WATCH={\"enable\":true,\"json\":true}\n"); };
        client_.onLine = [this](std::string_view line)
        {
            const auto j = nlohmann::json::parse(line, nullptr, /*allow_exceptions=*/false);
            if (!j.is_discarded() && apply(j) && onFix)
                onFix(state_);
        };
    }

    bool GpsdReceiver::apply(const nlohmann::json &msg)
    {
        if (!msg.is_object())
            return false;
        GnssState &s = state_;
        const std::string cls = msg.value("class", "");
        if (cls == "TPV")
        {
            s.mode = msg.value("mode", 0);
            if (const auto t = msg.find("time"); t != msg.end() && t->is_string())
            {
                if (const auto ms = parseIsoMs(t->get<std::string>()))
                    s.timeMs = *ms;
            }
            const bool fix = s.mode >= 2;
            s.lat = fix ? num(msg, "lat") : std::nullopt;
            s.lon = fix ? num(msg, "lon") : std::nullopt;
            s.altM = s.mode >= 3 ? (num(msg, "altMSL") ? num(msg, "altMSL") : num(msg, "alt")) : std::nullopt;
            s.speedKt = num(msg, "speed");
            if (s.speedKt)
                *s.speedKt *= kMsToKt;
            s.track = num(msg, "track");
            s.climbFpm = num(msg, "climb");
            if (s.climbFpm)
                *s.climbFpm *= kMToFt * 60;
            s.eph = num(msg, "eph");
            return true;
        }
        if (cls == "SKY")
        {
            if (const auto sats = msg.find("satellites"); sats != msg.end() && sats->is_array())
            {
                s.visible = static_cast<uint32_t>(sats->size());
                s.used = 0;
                for (const auto &sat : *sats)
                    s.used += sat.value("used", false) ? 1 : 0;
            }
            else if (const auto u = num(msg, "uSat"))
            {
                s.used = static_cast<uint32_t>(*u);
            }
            if (const auto n = num(msg, "nSat"))
                s.visible = static_cast<uint32_t>(*n);
            if (const auto h = num(msg, "hdop"))
                s.hdop = h;
            if (const auto v = num(msg, "vdop"))
                s.vdop = v;
        }
        return false;
    }

} // namespace dolo
