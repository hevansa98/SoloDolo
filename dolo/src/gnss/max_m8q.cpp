#include "gnss/max_m8q.hpp"

#include <algorithm>

#include "gnss/units.hpp"
#include "core/log.hpp"

namespace dolo
{
    namespace
    {
        constexpr double kMinTrackSpeedMs = 0.5; // below this the module's course over ground is noise
        constexpr double kUnknownDop = 99.0;     // the module reports 99.99 when it has no solution

        std::optional<double> knownDop(double v) { return v < kUnknownDop ? std::optional<double>(v) : std::nullopt; }
    } // namespace

    MaxM8qReceiver::MaxM8qReceiver(EventLoop &loop, std::string device, std::optional<int> baud)
        : UartGnssReceiver(loop, "MAX-M8Q", std::move(device), baud.value_or(kDefaultBaud)) {}

    std::string MaxM8qReceiver::configurationCommands()
    {
        std::string out;
        for (const uint8_t id : {ubx::kNavPvt, ubx::kNavDop, ubx::kNavSat})
            out += ubx::cfgMsg(ubx::kClassNav, id, 1);
        for (const uint8_t id : {ubx::kNmeaGll, ubx::kNmeaGsa, ubx::kNmeaGsv, ubx::kNmeaVtg})
            out += ubx::cfgMsg(ubx::kClassNmea, id, 0);
        return out;
    }

    void MaxM8qReceiver::onOpen()
    {
        write(configurationCommands());
    }

    void MaxM8qReceiver::onEpochStart()
    {
        gsaFresh_ = gsvFresh_ = true;
        ++silentEpochs_;
        if (silentEpochs_ == kUbxGraceEpochs + 1)
            state_.eph = state_.climbFpm = std::nullopt; // only UBX fills these; don't keep reporting stale values
        if (silentEpochs_ > kUbxGraceEpochs && silentEpochs_ % kReconfigureEveryEpochs == 0)
            write(configurationCommands());
    }

    bool MaxM8qReceiver::nmeaFixWanted() const
    {
        return silentEpochs_ > kUbxGraceEpochs;
    }

    void MaxM8qReceiver::onGsa(const nmea::Gsa &gsa)
    {
        // One GSA per constellation; the best fix type among them is the module's fix.
        fixType_ = gsaFresh_ ? gsa.fixType : std::max(fixType_, gsa.fixType);
        gsaFresh_ = false;
        state_.vdop = gsa.vdop; // the DOP fields are the combined solution's, repeated in every GSA
    }

    void MaxM8qReceiver::onGsv(const nmea::Gsv &gsv)
    {
        if (gsvFresh_)
        {
            viewByTalker_.clear(); // a constellation that dropped out of view stops being reported
            gsvFresh_ = false;
        }
        viewByTalker_[gsv.talker] = gsv.inView;
        state_.visible = 0;
        for (const auto &[talker, n] : viewByTalker_)
            state_.visible += n;
    }

    void MaxM8qReceiver::onUbx(const ubx::Frame &frame)
    {
        if (frame.cls == ubx::kClassAck && frame.payload.size() == 2)
        {
            const auto cls = static_cast<uint8_t>(frame.payload[0]), id = static_cast<uint8_t>(frame.payload[1]);
            if (frame.id == ubx::kAckNak)
                LOG_WARN("MAX-M8Q rejected UBX message %02X-%02X", cls, id);
            else
                LOG_DEBUG("MAX-M8Q accepted UBX message %02X-%02X", cls, id);
            return;
        }
        if (const auto pvt = ubx::decodeNavPvt(frame))
        {
            joinEpoch(pvt->iTow);
            silentEpochs_ = 0;
            applyPvt(*pvt);
            havePvt_ = true;
        }
        else if (const auto dop = ubx::decodeNavDop(frame))
        {
            joinEpoch(dop->iTow);
            state_.hdop = knownDop(dop->hdop);
            state_.vdop = knownDop(dop->vdop);
            haveDop_ = true;
        }
        else if (const auto sat = ubx::decodeNavSat(frame))
        {
            joinEpoch(sat->iTow);
            state_.visible = sat->visible;
            haveSat_ = true;
        }
        else
        {
            return;
        }
        publishIfComplete();
    }

    void MaxM8qReceiver::applyPvt(const ubx::NavPvt &pvt)
    {
        int mode = 1; // 4 (GNSS + dead reckoning) counts as 3D; 1 (dead reckoning only) and 5 (time only) as no fix
        if (pvt.fixOk && pvt.fixType == 2)
            mode = 2;
        else if (pvt.fixOk && (pvt.fixType == 3 || pvt.fixType == 4))
            mode = 3;

        state_.mode = mode;
        state_.used = pvt.satsUsed;
        if (pvt.timeMs)
            state_.timeMs = *pvt.timeMs;
        if (mode < 2)
        {
            state_.lat = state_.lon = state_.altM = std::nullopt;
            state_.speedKt = state_.track = state_.climbFpm = state_.eph = std::nullopt;
            return;
        }
        state_.lat = pvt.latDeg;
        state_.lon = pvt.lonDeg;
        state_.altM = mode == 3 ? std::optional<double>(pvt.hMslM) : std::nullopt;
        state_.speedKt = pvt.groundSpeedMs * kMsToKt;
        state_.track = pvt.groundSpeedMs >= kMinTrackSpeedMs ? std::optional<double>(pvt.headingDeg) : std::nullopt;
        state_.climbFpm = -pvt.velDownMs * kMToFt * 60.0;
        state_.eph = pvt.hAccM;
    }

    void MaxM8qReceiver::joinEpoch(uint32_t iTow)
    {
        if (iTow == epochItow_)
            return;
        if (havePvt_ && !published_)
            publish(); // the previous epoch never completed: a message is switched off or was lost
        epochItow_ = iTow;
        havePvt_ = haveDop_ = haveSat_ = published_ = false;
    }

    void MaxM8qReceiver::publishIfComplete()
    {
        if (!havePvt_ || !haveDop_ || !haveSat_ || published_)
            return;
        published_ = true;
        publish();
    }

} // namespace dolo
