#include "serial.hpp"

#include <fcntl.h>
#include <sys/epoll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "log.hpp"

namespace dolo {
namespace {

constexpr size_t kMaxLineBytes = 4096;  // NMEA sentences are at most 82 characters

speed_t baudConstant(int baud) {
    switch (baud) {
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return B0;
    }
}

}  // namespace

bool isSupportedBaud(int baud) { return baudConstant(baud) != B0; }

SerialLineReader::SerialLineReader(EventLoop& loop, std::string name, std::string device, int baud, double retryS)
    : loop_(loop), name_(std::move(name)), device_(std::move(device)), baud_(baud), retryS_(retryS) {}

SerialLineReader::~SerialLineReader() {
    if (retryTimer_) loop_.cancel(retryTimer_);
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
    }
}

void SerialLineReader::openNow() {
    retryTimer_ = 0;
    fd_ = open(device_.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        drop(std::strerror(errno));
        return;
    }
    termios tio{};
    if (tcgetattr(fd_, &tio) < 0) {
        drop(std::string("tcgetattr: ") + std::strerror(errno));
        return;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    cfsetispeed(&tio, baudConstant(baud_));
    cfsetospeed(&tio, baudConstant(baud_));
    if (tcsetattr(fd_, TCSANOW, &tio) < 0) {
        drop(std::string("tcsetattr: ") + std::strerror(errno));
        return;
    }
    tcflush(fd_, TCIFLUSH);
    loop_.add(fd_, EPOLLIN, [this](uint32_t ev) { onEvents(ev); });
    wasReachable_ = true;
    LOG_INFO("%s opened %s at %d baud", name_.c_str(), device_.c_str(), baud_);
}

void SerialLineReader::onEvents(uint32_t events) {
    if (!(events & (EPOLLIN | EPOLLHUP | EPOLLERR))) return;
    char buf[1024];
    for (;;) {
        const ssize_t n = read(fd_, buf, sizeof buf);
        if (n > 0) {
            in_.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            drop("closed");
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
    if (in_.size() > kMaxLineBytes) in_.clear();  // line noise; resync on the next newline
}

void SerialLineReader::drop(const std::string& why) {
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
        fd_ = -1;
    }
    if (wasReachable_) {
        LOG_WARN("%s %s unavailable (%s); retrying every %.0fs", name_.c_str(), device_.c_str(), why.c_str(), retryS_);
        wasReachable_ = false;
    }
    in_.clear();
    retryTimer_ = loop_.after(retryS_, [this] { openNow(); });
}

}  // namespace dolo
