#include "hub.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <google/protobuf/util/json_util.h>

#include "clock.hpp"
#include "framing.hpp"
#include "log.hpp"
#include "net.hpp"

namespace dolo {
namespace pb = solodolo::v1;
namespace {

constexpr size_t kQueueDepth = 8;
constexpr double kHelloTimeoutS = 10.0;

}  // namespace

pb::Envelope makeEnvelope() {
    pb::Envelope env;
    env.set_sent_unix_ms(unixMs());
    return env;
}

Hub::Hub(EventLoop& loop, std::vector<pb::Capability> capabilities)
    : loop_(loop), capabilities_(std::move(capabilities)) {
    // Clients that connect but never send ClientHello are dropped.
    sweepTimer_ = loop_.every(1.0, [this] {
        const double now = monotonicS();
        std::vector<int> stale;
        for (const auto& [fd, c] : conns_) {
            if (!c->debug && !c->helloed && now - c->acceptedAt > kHelloTimeoutS) stale.push_back(fd);
        }
        for (const int fd : stale) {
            LOG_INFO("client fd %d: no ClientHello; closing", fd);
            close(fd);
        }
    });
}

Hub::~Hub() {
    loop_.cancel(sweepTimer_);
    for (const int fd : listenFds_) {
        loop_.remove(fd);
        ::close(fd);
    }
    for (auto& [fd, c] : conns_) {
        loop_.remove(fd);
        ::close(fd);
    }
}

void Hub::listen(const std::string& host, int port) {
    const int fd = listenTcp(host, port);
    listenFds_.push_back(fd);
    loop_.add(fd, EPOLLIN, [this, fd](uint32_t) { accept(fd, false); });
}

void Hub::listenDebug(const std::string& host, int port) {
    const int fd = listenTcp(host, port);
    listenFds_.push_back(fd);
    loop_.add(fd, EPOLLIN, [this, fd](uint32_t) { accept(fd, true); });
}

size_t Hub::clientCount() const {
    size_t n = 0;
    for (const auto& [fd, c] : conns_) n += (!c->debug && c->helloed) ? 1 : 0;
    return n;
}

void Hub::accept(int listenFd, bool debug) {
    for (;;) {
        const int fd = ::accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;  // EAGAIN (drained) or a transient error; level-triggered, so we'll be called again
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        auto c = std::make_unique<Conn>();
        c->fd = fd;
        c->debug = debug;
        c->acceptedAt = monotonicS();
        Conn& ref = *c;
        conns_[fd] = std::move(c);
        loop_.add(fd, EPOLLIN, [this, fd](uint32_t ev) { onEvents(fd, ev); });
        if (debug) {
            LOG_INFO("debug viewer connected (fd %d)", fd);
            queue(ref, toJsonLine(serverHello()));
        }
    }
}

void Hub::onEvents(int fd, uint32_t events) {
    const auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = *it->second;
    if (events & EPOLLOUT) {
        flush(c);
        if (!conns_.count(fd)) return;
    }
    if (events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
        char buf[8192];
        for (;;) {
            const ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n > 0) {
                if (!c.debug) c.in.append(buf, static_cast<size_t>(n));  // debug viewers' input is discarded
                continue;
            }
            if (n == 0) {
                LOG_INFO("client fd %d disconnected", fd);
                close(fd);
                return;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_INFO("client fd %d: %s", fd, std::strerror(errno));
            close(fd);
            return;
        }
        if (!c.debug) readFrames(c);
    }
}

bool Hub::readFrames(Conn& c) {
    const int fd = c.fd;
    std::string payload;
    for (;;) {
        const FrameStatus st = extractFrame(c.in, payload);
        if (st == FrameStatus::NeedMore) return true;
        if (st == FrameStatus::Bad) {
            LOG_WARN("client fd %d: oversized frame; closing", fd);
            close(fd);
            return false;
        }
        pb::Envelope env;
        if (!env.ParseFromString(payload) || !handleEnvelope(c, env)) {
            if (conns_.count(fd)) close(fd);
            return false;
        }
    }
}

bool Hub::handleEnvelope(Conn& c, const pb::Envelope& env) {
    if (!c.helloed) {
        if (env.payload_case() != pb::Envelope::kClientHello) {
            LOG_WARN("client fd %d: first frame was not ClientHello", c.fd);
            return false;
        }
        if (env.client_hello().protocol_version() != kProtocolVersion) {
            LOG_WARN("client fd %d: unsupported protocol version %u", c.fd, env.client_hello().protocol_version());
            return false;
        }
        LOG_INFO("client fd %d connected (%s)", c.fd, env.client_hello().app_version().c_str());
        queue(c, toFrame(serverHello()));
        for (const auto& [kind, latest] : latest_) queue(c, toFrame(latest));
        c.helloed = true;
        return true;
    }
    if (env.payload_case() == pb::Envelope::kPing) {
        pb::Envelope pong = makeEnvelope();
        pong.mutable_pong()->set_seq(env.ping().seq());
        queue(c, toFrame(pong));
    }
    return true;
}

pb::Envelope Hub::serverHello() const {
    pb::Envelope env = makeEnvelope();
    auto* h = env.mutable_server_hello();
    h->set_protocol_version(kProtocolVersion);
    h->set_bridge_version(DOLO_VERSION);
    for (const auto cap : capabilities_) h->add_capabilities(cap);
    h->set_pps_locked(false);  // no PPS discipline yet
    return env;
}

void Hub::broadcast(const pb::Envelope& env) {
    latest_[env.payload_case()] = env;
    std::string frame, json;
    for (auto& [fd, c] : conns_) {
        if (c->debug) {
            if (json.empty()) json = toJsonLine(env);
            queue(*c, json);
        } else if (c->helloed) {
            if (frame.empty()) frame = toFrame(env);
            queue(*c, frame);
        }
    }
}

void Hub::queue(Conn& c, std::string data) {
    if (c.dead) return;
    // Never drop the frame being written (index 0 once partially sent).
    while (c.out.size() >= kQueueDepth) {
        if (c.out.size() > 1 || c.frontSent == 0) {
            c.out.erase(c.frontSent == 0 ? c.out.begin() : c.out.begin() + 1);
        } else {
            return;
        }
    }
    c.out.push_back(std::move(data));
    flush(c);
}

void Hub::flush(Conn& c) {
    const int fd = c.fd;
    while (!c.out.empty()) {
        const std::string& front = c.out.front();
        const ssize_t n = ::send(fd, front.data() + c.frontSent, front.size() - c.frontSent, MSG_NOSIGNAL);
        if (n > 0) {
            c.frontSent += static_cast<size_t>(n);
            if (c.frontSent == front.size()) {
                c.out.pop_front();
                c.frontSent = 0;
            }
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            loop_.modify(fd, EPOLLIN | EPOLLOUT);
            return;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            markDead(c);
            return;
        }
    }
    loop_.modify(fd, EPOLLIN);
}

void Hub::markDead(Conn& c) {
    if (c.dead) return;
    c.dead = true;
    c.out.clear();
    const int fd = c.fd;
    loop_.after(0, [this, fd] { close(fd); });
}

void Hub::close(int fd) {
    if (conns_.erase(fd) == 0) return;
    loop_.remove(fd);
    ::close(fd);
}

std::string Hub::toFrame(const pb::Envelope& env) { return encodeFrame(env.SerializeAsString()); }

std::string Hub::toJsonLine(const pb::Envelope& env) {
    google::protobuf::util::JsonPrintOptions opts;
    opts.add_whitespace = false;
    std::string json;
    (void)google::protobuf::util::MessageToJsonString(env, &json, opts);
    return json + "\n";
}

}  // namespace dolo
