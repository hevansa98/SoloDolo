// server/: length-prefixed framing, and the hub's handshake, replay and fan-out over real sockets.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>

#include "server/framing.hpp"
#include "server/hub.hpp"
#include "support.hpp"

TEST(Framing, EncodesAndExtractsFrames) {
    EXPECT_EQ(encodeFrame("abc"), std::string("\x00\x00\x00\x03" "abc", 7));
    EXPECT_THROW(encodeFrame(std::string(kMaxFrameBytes + 1, 'x')), std::length_error);

    std::string buf = encodeFrame("one") + encodeFrame("two") + std::string("\x00\x00", 2), out;
    EXPECT_EQ(extractFrame(buf, out), FrameStatus::Frame);
    EXPECT_EQ(out, "one");
    EXPECT_EQ(extractFrame(buf, out), FrameStatus::Frame);
    EXPECT_EQ(out, "two");
    EXPECT_EQ(extractFrame(buf, out), FrameStatus::NeedMore);  // a length prefix with no body yet
    std::string huge("\x00\x10\x00\x01", 4);  // 1 MiB + 1
    EXPECT_EQ(extractFrame(huge, out), FrameStatus::Bad);
}

static int connectTo(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) { close(fd); return -1; }
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return fd;
}

static bool readExactly(int fd, void* p, size_t n) {
    auto* b = static_cast<char*>(p);
    while (n) {
        const ssize_t r = recv(fd, b, n, 0);
        if (r <= 0) return false;
        b += r; n -= static_cast<size_t>(r);
    }
    return true;
}

static std::optional<pb::Envelope> readEnvelope(int fd) {
    unsigned char hdr[4];
    if (!readExactly(fd, hdr, 4)) return std::nullopt;
    std::string body((hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3], '\0');
    if (!readExactly(fd, body.data(), body.size())) return std::nullopt;
    pb::Envelope env;
    if (!env.ParseFromString(body)) return std::nullopt;
    return env;
}

static void sendEnvelope(int fd, const pb::Envelope& env) {
    const std::string f = encodeFrame(env.SerializeAsString());
    send(fd, f.data(), f.size(), 0);
}

static pb::Envelope helloWith(uint32_t version) {
    auto env = makeEnvelope();
    env.mutable_client_hello()->set_protocol_version(version);
    env.mutable_client_hello()->set_app_version("test");
    return env;
}

// A blocking client on a thread talks to the hub running on the main thread. The client uses EXPECT_* only (never
// ASSERT_*) and a guard that always stops the loop, so a failing check cannot leave the main thread running forever.
TEST(Hub, HandshakeKeepaliveReplayAndDebugMirror) {
    EventLoop loop;
    Hub hub(loop, {pb::CAPABILITY_TRAFFIC});
    constexpr int kPort = 45555, kDebug = 45556;
    hub.listen("127.0.0.1", kPort);
    hub.listenDebug("127.0.0.1", kDebug);

    int counter = 0;
    loop.every(0.05, [&] {
        auto env = makeEnvelope();
        env.mutable_traffic()->set_messages_total(++counter);
        hub.broadcast(env);
    });
    loop.after(20.0, [&] {
        ADD_FAILURE() << "hub test timed out";
        loop.stop();
    });

    std::thread client([&] {
        struct StopLoop {
            EventLoop &loop;
            ~StopLoop() { loop.stop(); }
        } stopLoop{loop};

        // Wrong protocol version: closed without a ServerHello.
        int bad = connectTo(kPort);
        EXPECT_GE(bad, 0);
        sendEnvelope(bad, helloWith(99));
        EXPECT_FALSE(readEnvelope(bad));
        close(bad);

        // Oversized frame: closed.
        bad = connectTo(kPort);
        const unsigned char huge[4] = {0x00, 0x10, 0x00, 0x01};
        send(bad, huge, 4, 0);
        EXPECT_FALSE(readEnvelope(bad));
        close(bad);

        const int dbg = connectTo(kDebug);
        const int fd = connectTo(kPort);
        EXPECT_GE(fd, 0);
        EXPECT_GE(dbg, 0);
        sendEnvelope(fd, helloWith(1));
        const auto hello = readEnvelope(fd);
        EXPECT_TRUE(hello.has_value());
        if (hello) {
            EXPECT_EQ(hello->payload_case(), pb::Envelope::kServerHello);
            EXPECT_EQ(hello->server_hello().protocol_version(), 1u);
            EXPECT_EQ(hello->server_hello().capabilities_size(), 1);
        }

        auto ping = makeEnvelope();
        ping.mutable_ping()->set_seq(42);
        sendEnvelope(fd, ping);
        bool gotPong = false, gotTraffic = false;
        for (int i = 0; i < 20 && !(gotPong && gotTraffic); ++i) {
            const auto env = readEnvelope(fd);
            if (!env) break;
            gotPong |= env->payload_case() == pb::Envelope::kPong && env->pong().seq() == 42;
            gotTraffic |= env->payload_case() == pb::Envelope::kTraffic;
        }
        EXPECT_TRUE(gotPong);
        EXPECT_TRUE(gotTraffic);

        char buf[512] = {};
        std::string seen;
        while (seen.find("\"traffic\"") == std::string::npos) {
            const ssize_t n = recv(dbg, buf, sizeof buf - 1, 0);
            if (n <= 0) break;
            seen.append(buf, static_cast<size_t>(n));
        }
        EXPECT_NE(seen.find("\"serverHello\""), std::string::npos);
        EXPECT_NE(seen.find("\"messagesTotal\""), std::string::npos);
        EXPECT_NE(seen.find('\n'), std::string::npos);
        close(fd);
        close(dbg);
    });
    loop.run();
    client.join();
}
