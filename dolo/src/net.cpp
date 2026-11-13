#include "net.hpp"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "log.hpp"

namespace dolo {
namespace {

constexpr size_t kMaxLineBytes = 1u << 20;

struct AddrInfoDeleter { void operator()(addrinfo* p) const { freeaddrinfo(p); } };
using AddrInfo = std::unique_ptr<addrinfo, AddrInfoDeleter>;

AddrInfo resolve(const std::string& host, int port, bool passive, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (passive) hints.ai_flags = AI_PASSIVE;
    addrinfo* res = nullptr;
    const int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0) {
        error = gai_strerror(rc);
        return nullptr;
    }
    return AddrInfo(res);
}

}  // namespace

void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) throw std::runtime_error("fcntl O_NONBLOCK failed");
}

int listenTcp(const std::string& host, int port) {
    std::string err;
    const AddrInfo ai = resolve(host, port, true, err);
    if (!ai) throw std::runtime_error("cannot resolve " + host + ": " + err);
    const int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0 || listen(fd, 16) < 0) {
        const std::string msg = "cannot listen on " + host + ":" + std::to_string(port) + ": " + std::strerror(errno);
        close(fd);
        throw std::runtime_error(msg);
    }
    setNonBlocking(fd);
    return fd;
}

TcpClient::TcpClient(EventLoop& loop, std::string name, std::string host, int port, double retryS)
    : loop_(loop), name_(std::move(name)), host_(std::move(host)), port_(port), retryS_(retryS) {}

TcpClient::~TcpClient() {
    if (retryTimer_) loop_.cancel(retryTimer_);
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
    }
}

void TcpClient::connectNow() {
    retryTimer_ = 0;
    std::string err;
    const AddrInfo ai = resolve(host_, port_, false, err);
    if (!ai) {
        drop("cannot resolve " + host_ + ": " + err);
        return;
    }
    fd_ = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        drop(std::string("socket: ") + std::strerror(errno));
        return;
    }
    if (connect(fd_, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) {
        drop(std::strerror(errno));
        return;
    }
    loop_.add(fd_, EPOLLIN | EPOLLOUT, [this](uint32_t ev) { onEvents(ev); });
}

void TcpClient::onEvents(uint32_t events) {
    if (!connected_) {
        int soErr = 0;
        socklen_t len = sizeof soErr;
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soErr, &len);
        if (soErr != 0) {
            drop(std::strerror(soErr));
            return;
        }
        if (!(events & (EPOLLOUT | EPOLLIN))) return;
        connected_ = true;
        wasReachable_ = true;
        LOG_INFO("%s connected to %s:%d", name_.c_str(), host_.c_str(), port_);
        loop_.modify(fd_, EPOLLIN);
        if (onConnect) onConnect();
        if (fd_ < 0) return;  // onConnect may have dropped us
    }
    if (events & EPOLLOUT) flush();
    if (fd_ < 0) return;
    if (events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
        char buf[16384];
        for (;;) {
            const ssize_t n = recv(fd_, buf, sizeof buf, 0);
            if (n > 0) {
                in_.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                drop("closed by peer");
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            drop(std::strerror(errno));
            return;
        }
        size_t start = 0;
        for (size_t nl; (nl = in_.find('\n', start)) != std::string::npos; start = nl + 1) {
            if (onLine) onLine(std::string_view(in_).substr(start, nl - start));
            if (fd_ < 0) return;
        }
        in_.erase(0, start);
        if (in_.size() > kMaxLineBytes) drop("line too long");
    }
}

void TcpClient::send(std::string_view data) {
    if (!connected_) return;
    out_.append(data);
    flush();
}

void TcpClient::flush() {
    while (!out_.empty()) {
        const ssize_t n = ::send(fd_, out_.data(), out_.size(), MSG_NOSIGNAL);
        if (n > 0) {
            out_.erase(0, static_cast<size_t>(n));
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            loop_.modify(fd_, EPOLLIN | EPOLLOUT);
            return;
        } else if (errno != EINTR) {
            drop(std::strerror(errno));
            return;
        }
    }
    loop_.modify(fd_, EPOLLIN);
}

void TcpClient::drop(const std::string& why) {
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
        fd_ = -1;
    }
    if (connected_) {
        LOG_WARN("%s connection lost (%s); retrying", name_.c_str(), why.c_str());
        wasReachable_ = true;
    } else if (wasReachable_) {
        LOG_INFO("%s %s:%d unavailable (%s); retrying every %.0fs", name_.c_str(), host_.c_str(), port_, why.c_str(), retryS_);
        wasReachable_ = false;
    }
    connected_ = false;
    in_.clear();
    out_.clear();
    retryTimer_ = loop_.after(retryS_, [this] { connectNow(); });
}

}  // namespace dolo
