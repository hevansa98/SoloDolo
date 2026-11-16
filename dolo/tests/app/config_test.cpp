// app/: YAML configuration loading and validation.
#include "app/config.hpp"
#include "support.hpp"

TEST(Config, LoadsDefaultsAndRejectsBadValues) {
    const Config d = Config::fromYaml("");
    EXPECT_EQ(d.port, 5555);
    EXPECT_EQ(d.debugPort, 5556);
    EXPECT_FALSE(d.gnssEnabled);
    EXPECT_FALSE(d.fallbackLat);

    const Config c = Config::fromYaml(
        "server: {port: 6000, debug_port: null}\n"
        "gnss: {enabled: true}\n"
        "ownship_fallback: {lat: 1.5, lon: 2.5}\n"
        "log_level: debug\n");
    EXPECT_EQ(c.port, 6000);
    EXPECT_FALSE(c.debugPort);
    EXPECT_TRUE(c.gnssEnabled);
    EXPECT_EQ(*c.fallbackLat, 1.5);
    EXPECT_EQ(c.logLevel, "debug");

    EXPECT_EQ(d.gnss.receiver, "max-m8q");
    EXPECT_EQ(d.gnss.device, "/dev/ttyAMA0");
    EXPECT_FALSE(d.gnss.baud);
    const Config s = Config::fromYaml("gnss: {enabled: true, receiver: gt-u7, device: /dev/ttyUSB0, baud: 38400}");
    EXPECT_EQ(s.gnss.receiver, "gt-u7");
    EXPECT_EQ(s.gnss.device, "/dev/ttyUSB0");
    EXPECT_EQ(s.gnss.baud, 38400);
    EXPECT_THROW(Config::fromYaml("gnss: {receiver: usb}"), std::exception);
    EXPECT_THROW(Config::fromYaml("gnss: {source: serial}"), std::exception);  // replaced by "receiver"
    EXPECT_THROW(Config::fromYaml("gnss: {baud: 1234}"), std::exception);

    EXPECT_THROW(Config::fromYaml("sever: {}"), std::exception);                          // typo'd key
    EXPECT_THROW(Config::fromYaml("server: {prot: 1}"), std::exception);
    EXPECT_THROW(Config::fromYaml("server: {port: 99999}"), std::exception);
    EXPECT_THROW(Config::fromYaml("server: {port: 5555, debug_port: 5555}"), std::exception);
    EXPECT_THROW(Config::fromYaml("server: {port: abc}"), std::exception);
    EXPECT_THROW(Config::fromYaml("ownship_fallback: {lat: 1}"), std::exception);
    EXPECT_THROW(Config::fromYaml("traffic_hz: 0"), std::exception);
    EXPECT_THROW(Config::fromYaml("log_level: loud"), std::exception);
    EXPECT_THROW(Config::fromFile("/nonexistent/dolo.yaml"), std::exception);
}

// ---- Hub integration: a blocking client on a thread talks to the hub running on the main thread ----
