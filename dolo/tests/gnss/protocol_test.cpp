// gnss/ protocol codecs: NMEA sentences, UBX frames and messages, and the stream splitter.
#include <string>
#include <vector>

#include "fixtures.hpp"
#include "gnss/nmea.hpp"
#include "gnss/stream.hpp"
#include "gnss/ubx.hpp"
#include "support.hpp"

TEST(Nmea, ParsesSentencesAndRejectsBadInput) {
    const auto gga = nmea::parse(sentence("GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,"));
    ASSERT_TRUE(gga);
    EXPECT_TRUE(std::holds_alternative<nmea::Gga>(*gga));
    const auto& g = std::get<nmea::Gga>(*gga);
    EXPECT_NEAR(*g.lat, 40.5, 1e-9);
    EXPECT_NEAR(*g.lon, -75.25, 1e-9);
    EXPECT_EQ(g.quality, 1);
    EXPECT_EQ(g.satsUsed, 7);

    EXPECT_FALSE(nmea::parse("$GPGGA,010203.50,4030.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,*00"));  // bad checksum
    EXPECT_FALSE(nmea::parse("GPGGA,no dollar sign"));
    EXPECT_FALSE(nmea::parse(""));
    EXPECT_FALSE(nmea::parse(sentence("GPVTG,,T,,M,0.0,N,0.0,K,A")));  // a sentence Dolo does not use
    const auto bad = nmea::parse(sentence("GPGGA,010203.50,9900.000,N,07515.000,W,1,07,0.9,100.0,M,-34.0,M,,"));
    ASSERT_TRUE(bad);
    EXPECT_FALSE(std::get<nmea::Gga>(*bad).lat);  // latitude out of range is dropped
}

TEST(Ubx, EncodesAndDecodesMessages) {
    EXPECT_EQ(ubx::cfgMsg(0x01, 0x07, 1), std::string("\xb5\x62\x06\x01\x03\x00\x01\x07\x01\x13\x51", 11));  // checksum from an independent calculation

    std::vector<ubx::Frame> frames;
    StreamSplitter split;
    split.onUbx = [&](const ubx::Frame &f) { frames.push_back(f); };
    split.feed(kRealNavPvt + kRealNavDop + navSat(15554000, 25, 16));
    EXPECT_EQ(frames.size(), 3);

    const auto pvt = ubx::decodeNavPvt(frames[0]);
    ASSERT_TRUE(pvt);
    EXPECT_EQ(pvt->iTow, 15554000);
    EXPECT_EQ(pvt->fixType, 3);
    EXPECT_TRUE(pvt->fixOk);
    EXPECT_EQ(pvt->satsUsed, 16);
    EXPECT_NEAR(pvt->latDeg, 33.0448126, 1e-9);
    EXPECT_NEAR(pvt->lonDeg, -96.9884181, 1e-9);
    EXPECT_NEAR(pvt->hMslM, 144.195, 1e-9);
    EXPECT_NEAR(pvt->hAccM, 2.432, 1e-9);
    EXPECT_NEAR(pvt->groundSpeedMs, 0.027, 1e-9);
    EXPECT_NEAR(pvt->velDownMs, 0.090, 1e-9);
    EXPECT_EQ(pvt->timeMs, 1794716336000ULL);  // 2026-11-15T04:18:56Z; the module's fraction is 0.48 ms

    const auto dop = ubx::decodeNavDop(frames[1]);
    ASSERT_TRUE(dop);
    EXPECT_EQ(dop->iTow, 15554000);
    EXPECT_NEAR(dop->hdop, 0.70, 1e-9);
    EXPECT_NEAR(dop->vdop, 1.30, 1e-9);

    const auto sat = ubx::decodeNavSat(frames[2]);
    ASSERT_TRUE(sat);
    EXPECT_EQ(sat->visible, 25);
    EXPECT_EQ(sat->used, 16);

    // Decoders reject the wrong message and truncated payloads.
    EXPECT_FALSE(ubx::decodeNavPvt(frames[1]));
    EXPECT_FALSE(ubx::decodeNavDop(frames[0]));
    EXPECT_FALSE(ubx::decodeNavSat(frames[0]));
    ubx::Frame shortPvt = frames[0];
    shortPvt.payload.resize(91);
    EXPECT_FALSE(ubx::decodeNavPvt(shortPvt));
    ubx::Frame shortSat = frames[2];
    shortSat.payload.resize(shortSat.payload.size() - 1);
    EXPECT_FALSE(ubx::decodeNavSat(shortSat));
}

TEST(StreamSplitter, SeparatesNmeaFromUbxInAnyChunking) {
    std::vector<std::string> lines;
    std::vector<ubx::Frame> frames;
    StreamSplitter split;
    split.onNmea = [&](std::string_view l) { lines.emplace_back(l); };
    split.onUbx = [&](const ubx::Frame &f) { frames.push_back(f); };

    // A UBX payload full of bytes that look like NMEA framing: '$' and newlines.
    const std::string tricky = ubx::encode(0x0a, 0x04, std::string("$GPGGA,\n\r\n\n$", 12));
    std::string corrupt = ubx::encode(0x0a, 0x05, "xyz");
    corrupt[corrupt.size() - 1] ^= 0x55;  // bad checksum
    const std::string stream = "$GPRMC,one*00\r\n" + tricky + "\x01\x02garbage" + corrupt + std::string("\xb5\x00\xb5", 3) +
                               "$GPGGA,two*00\r\n" + kRealNavDop;

    for (const char c : stream) split.feed(std::string_view(&c, 1));  // worst case chunking: one byte at a time
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "$GPRMC,one*00\r\n");
    EXPECT_EQ(lines[1], "$GPGGA,two*00\r\n");
    EXPECT_EQ(frames.size(), 2);
    EXPECT_EQ(frames[0].cls, 0x0a);
    EXPECT_EQ(frames[0].id, 0x04);
    EXPECT_EQ(frames[0].payload, std::string("$GPGGA,\n\r\n\n$", 12));
    EXPECT_EQ(frames[1].cls, ubx::kClassNav);
    EXPECT_EQ(frames[1].id, ubx::kNavDop);

    // The same stream in one chunk gives the same result.
    lines.clear();
    frames.clear();
    split.feed(stream);
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(frames.size(), 2);

    // A '$' with no newline for hundreds of bytes is dropped and the stream recovers.
    lines.clear();
    split.feed("$" + std::string(300, 'x') + "\n$GPGGA,three*00\r\n");
    EXPECT_FALSE(lines.empty());
    EXPECT_EQ(lines.back(), "$GPGGA,three*00\r\n");
}
