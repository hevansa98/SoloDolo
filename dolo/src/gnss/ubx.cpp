#include "gnss/ubx.hpp"

#include <ctime>

namespace dolo::ubx
{
    namespace
    {

        uint8_t u8(std::string_view p, size_t at) { return static_cast<uint8_t>(p[at]); }

        uint16_t u16(std::string_view p, size_t at)
        {
            return static_cast<uint16_t>(u8(p, at) | u8(p, at + 1) << 8);
        }

        uint32_t u32(std::string_view p, size_t at)
        {
            return static_cast<uint32_t>(u8(p, at)) | static_cast<uint32_t>(u8(p, at + 1)) << 8 |
                   static_cast<uint32_t>(u8(p, at + 2)) << 16 | static_cast<uint32_t>(u8(p, at + 3)) << 24;
        }

        int32_t i32(std::string_view p, size_t at) { return static_cast<int32_t>(u32(p, at)); }

    } // namespace

    std::pair<uint8_t, uint8_t> checksum(uint8_t cls, uint8_t id, std::string_view payload)
    {
        uint8_t a = 0, b = 0;
        auto add = [&](uint8_t byte)
        {
            a = static_cast<uint8_t>(a + byte);
            b = static_cast<uint8_t>(b + a);
        };
        add(cls);
        add(id);
        add(static_cast<uint8_t>(payload.size() & 0xff));
        add(static_cast<uint8_t>(payload.size() >> 8));
        for (const char c : payload)
            add(static_cast<uint8_t>(c));
        return {a, b};
    }

    std::string encode(uint8_t cls, uint8_t id, std::string_view payload)
    {
        std::string out;
        out.reserve(kOverheadBytes + payload.size());
        out.push_back(static_cast<char>(kSync1));
        out.push_back(static_cast<char>(kSync2));
        out.push_back(static_cast<char>(cls));
        out.push_back(static_cast<char>(id));
        out.push_back(static_cast<char>(payload.size() & 0xff));
        out.push_back(static_cast<char>(payload.size() >> 8));
        out.append(payload);
        const auto [a, b] = checksum(cls, id, payload);
        out.push_back(static_cast<char>(a));
        out.push_back(static_cast<char>(b));
        return out;
    }

    std::string cfgMsg(uint8_t cls, uint8_t id, uint8_t rate)
    {
        const char payload[3] = {static_cast<char>(cls), static_cast<char>(id), static_cast<char>(rate)};
        return encode(kClassCfg, kCfgMsg, std::string_view(payload, 3));
    }

    std::optional<NavPvt> decodeNavPvt(const Frame &f)
    {
        if (f.cls != kClassNav || f.id != kNavPvt || f.payload.size() < 92)
            return std::nullopt;
        const std::string_view p = f.payload;
        NavPvt r;
        r.iTow = u32(p, 0);
        if ((u8(p, 11) & 0x03) == 0x03) // validDate and validTime
        {
            std::tm tm{};
            tm.tm_year = u16(p, 4) - 1900;
            tm.tm_mon = u8(p, 6) - 1;
            tm.tm_mday = u8(p, 7);
            tm.tm_hour = u8(p, 8);
            tm.tm_min = u8(p, 9);
            tm.tm_sec = u8(p, 10);
            // nano is the fraction of the second, negative when the rounded second is ahead of the true time.
            const int64_t ms = static_cast<int64_t>(timegm(&tm)) * 1000 + i32(p, 16) / 1000000;
            if (ms > 0)
                r.timeMs = static_cast<uint64_t>(ms);
        }
        r.fixType = u8(p, 20);
        r.fixOk = (u8(p, 21) & 0x01) != 0;
        r.satsUsed = u8(p, 23);
        r.lonDeg = i32(p, 24) * 1e-7;
        r.latDeg = i32(p, 28) * 1e-7;
        r.hMslM = i32(p, 36) * 1e-3;
        r.hAccM = u32(p, 40) * 1e-3;
        r.vAccM = u32(p, 44) * 1e-3;
        r.velDownMs = i32(p, 56) * 1e-3;
        r.groundSpeedMs = i32(p, 60) * 1e-3;
        r.headingDeg = i32(p, 64) * 1e-5;
        return r;
    }

    std::optional<NavDop> decodeNavDop(const Frame &f)
    {
        if (f.cls != kClassNav || f.id != kNavDop || f.payload.size() < 18)
            return std::nullopt;
        const std::string_view p = f.payload;
        return NavDop{u32(p, 0), u16(p, 12) * 0.01, u16(p, 10) * 0.01};
    }

    std::optional<NavSat> decodeNavSat(const Frame &f)
    {
        if (f.cls != kClassNav || f.id != kNavSat || f.payload.size() < 8)
            return std::nullopt;
        const std::string_view p = f.payload;
        const size_t n = u8(p, 5);
        if (p.size() < 8 + 12 * n)
            return std::nullopt;
        NavSat r;
        r.iTow = u32(p, 0);
        r.visible = static_cast<uint32_t>(n);
        for (size_t i = 0; i < n; ++i)
        {
            if (u32(p, 8 + 12 * i + 8) & 0x08) // flags bit 3: svUsed
                ++r.used;
        }
        return r;
    }

} // namespace dolo::ubx
