// Accepts iPad clients (protobuf, :5555) and debug viewers (NDJSON, :5556) and fans Envelopes out to them.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "event_loop.hpp"
#include "solodolo.pb.h"

namespace dolo {

constexpr uint32_t kProtocolVersion = 1;

solodolo::v1::Envelope makeEnvelope();  // stamped with the current time

class Hub {
public:
    Hub(EventLoop& loop, std::vector<solodolo::v1::Capability> capabilities);
    ~Hub();

    void listen(const std::string& host, int port);       // protobuf clients
    void listenDebug(const std::string& host, int port);  // read-only JSON mirror

    void broadcast(const solodolo::v1::Envelope& env);
    size_t clientCount() const;

private:
    struct Conn {
        int fd;
        bool debug;
        bool helloed = false;
        bool dead = false;  // write failed; closed on the next loop turn so callers' references stay valid
        double acceptedAt;
        std::string in;
        std::deque<std::string> out;  // whole frames; a slow client loses its oldest unsent ones
        size_t frontSent = 0;
    };

    void accept(int listenFd, bool debug);
    void onEvents(int fd, uint32_t events);
    bool readFrames(Conn& c);  // false if the connection was closed
    bool handleEnvelope(Conn& c, const solodolo::v1::Envelope& env);
    void queue(Conn& c, std::string data);
    void flush(Conn& c);
    void close(int fd);
    void markDead(Conn& c);
    solodolo::v1::Envelope serverHello() const;
    static std::string toFrame(const solodolo::v1::Envelope& env);
    static std::string toJsonLine(const solodolo::v1::Envelope& env);

    EventLoop& loop_;
    std::vector<solodolo::v1::Capability> capabilities_;
    std::vector<int> listenFds_;
    std::unordered_map<int, std::unique_ptr<Conn>> conns_;
    std::map<int, solodolo::v1::Envelope> latest_;  // by payload case; replayed to new clients after hello
    EventLoop::TimerId sweepTimer_;
};

}  // namespace dolo
