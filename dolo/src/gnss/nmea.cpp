#include "gnss/nmea.hpp"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <vector>

namespace dolo::nmea
{
    namespace
    {

        /// Verifies "$....*HH" and returns the body between '$' and '*', or nullopt.
        std::optional<std::string_view> checkedBody(std::string_view line)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                line.remove_suffix(1);
            const size_t star = line.rfind('*');
            if (line.size() < 4 || line.front() != '$' || star == std::string_view::npos || star + 3 != line.size())
                return std::nullopt;
            const std::string_view body = line.substr(1, star - 1);
            unsigned sum = 0;
            for (const char c : body)
                sum ^= static_cast<unsigned char>(c);
            char *end = nullptr;
            const std::string hex(line.substr(star + 1));
            const unsigned long want = std::strtoul(hex.c_str(), &end, 16);
            if (*end != '\0' || want != sum)
                return std::nullopt;
            return body;
        }

        std::vector<std::string_view> splitFields(std::string_view body)
        {
            std::vector<std::string_view> out;
            for (size_t pos = 0;;)
            {
                const size_t comma = body.find(',', pos);
                out.push_back(body.substr(pos, comma == std::string_view::npos ? comma : comma - pos));
                if (comma == std::string_view::npos)
                    return out;
                pos = comma + 1;
            }
        }

        std::optional<double> num(const std::vector<std::string_view> &f, size_t i)
        {
            if (i >= f.size() || f[i].empty())
                return std::nullopt;
            const std::string s(f[i]);
            char *end = nullptr;
            const double v = std::strtod(s.c_str(), &end);
            if (*end != '\0' || !std::isfinite(v))
                return std::nullopt;
            return v;
        }

        /// Receivers report 99.99 for "unknown" dilution of precision.
        std::optional<double> dop(std::optional<double> v) { return v && *v < 99.0 ? v : std::nullopt; }

        /// "ddmm.mmmm" + hemisphere -> signed degrees. degDigits is 2 for latitude, 3 for longitude.
        std::optional<double> coord(const std::vector<std::string_view> &f, size_t i, int degDigits)
        {
            const auto raw = num(f, i);
            if (!raw || i + 1 >= f.size() || f[i + 1].size() != 1)
                return std::nullopt;
            const double deg = std::floor(*raw / 100.0), min = *raw - deg * 100.0;
            if (min >= 60.0 || deg >= (degDigits == 2 ? 91 : 181))
                return std::nullopt;
            double v = deg + min / 60.0;
            const char h = f[i + 1][0];
            if (h == 'S' || h == 'W')
                v = -v;
            else if (h != 'N' && h != 'E')
                return std::nullopt;
            return v;
        }

        /// "hhmmss.ss" + "ddmmyy" -> Unix ms.
        std::optional<uint64_t> timeMs(std::string_view hms, std::string_view dmy)
        {
            if (hms.size() < 6 || dmy.size() != 6)
                return std::nullopt;
            for (size_t i = 0; i < 6; ++i)
                if (hms[i] < '0' || hms[i] > '9' || dmy[i] < '0' || dmy[i] > '9')
                    return std::nullopt;
            auto two = [](std::string_view s, size_t at)
            { return (s[at] - '0') * 10 + (s[at + 1] - '0'); };
            std::tm tm{};
            tm.tm_hour = two(hms, 0);
            tm.tm_min = two(hms, 2);
            tm.tm_sec = two(hms, 4);
            tm.tm_mday = two(dmy, 0);
            tm.tm_mon = two(dmy, 2) - 1;
            tm.tm_year = 100 + two(dmy, 4); // GPS dates are 2000-2099
            if (tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 60 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_mon < 0 || tm.tm_mon > 11)
                return std::nullopt;
            uint64_t ms = static_cast<uint64_t>(timegm(&tm)) * 1000;
            if (hms.size() > 7 && hms[6] == '.')
            {
                int digits = 0, frac = 0;
                for (size_t i = 7; i < hms.size() && hms[i] >= '0' && hms[i] <= '9' && digits < 3; ++i, ++digits)
                    frac = frac * 10 + (hms[i] - '0');
                while (digits++ < 3)
                    frac *= 10;
                ms += frac;
            }
            return ms;
        }

    } // namespace

    std::optional<Sentence> parse(std::string_view line)
    {
        const auto body = checkedBody(line);
        if (!body || body->size() < 6 || (*body)[5] != ',')
            return std::nullopt;
        const std::string_view talker = body->substr(0, 2), type = body->substr(2, 3);
        const auto f = splitFields(*body);

        if (type == "RMC")
        {
            // $--RMC,time,status,lat,N,lon,E,speed(kt),course,date,...
            if (f.size() < 10)
                return std::nullopt;
            Rmc r;
            r.valid = f[2] == "A";
            if (r.valid)
            {
                r.timeMs = timeMs(f[1], f[9]);
                r.speedKt = num(f, 7);
                r.trackDeg = num(f, 8); // empty while stationary
            }
            return r;
        }
        if (type == "GGA")
        {
            // $--GGA,time,lat,N,lon,E,quality,sats used,hdop,alt MSL,M,...
            if (f.size() < 10)
                return std::nullopt;
            Gga g;
            g.quality = static_cast<int>(num(f, 6).value_or(0));
            g.lat = coord(f, 2, 2);
            g.lon = coord(f, 4, 3);
            g.satsUsed = static_cast<uint32_t>(num(f, 7).value_or(0));
            g.hdop = dop(num(f, 8));
            g.altMslM = num(f, 9);
            return g;
        }
        if (type == "GSA")
        {
            // $--GSA,A,fixtype(1/2/3),sat1..sat12,pdop,hdop,vdop
            if (f.size() < 18)
                return std::nullopt;
            Gsa g;
            g.fixType = static_cast<int>(num(f, 2).value_or(0));
            g.vdop = dop(num(f, 17));
            return g;
        }
        if (type == "GSV")
        {
            // $--GSV,total msgs,msg #,sats in view,...
            if (f.size() < 4)
                return std::nullopt;
            const auto n = num(f, 3);
            if (!n)
                return std::nullopt;
            return Gsv{std::string(talker), static_cast<uint32_t>(*n)};
        }
        return std::nullopt;
    }

} // namespace dolo::nmea
