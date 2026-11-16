#include "io/serial.hpp"

#include <fcntl.h>
#include <sys/epoll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/log.hpp"

namespace dolo {
namespace {

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

SerialPort::SerialPort(EventLoop& loop, std::string name, std::string device, int baud, double retryS)
    : loop_(loop), name_(std::move(name)), device_(std::move(device)), baud_(baud), retryS_(retryS) {}

SerialPort::~SerialPort() {
    if (retryTimer_) loop_.cancel(retryTimer_);
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
    }
}

void SerialPort::openNow() {
    retryTimer_ = 0;
    fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
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
    if (onOpen) onOpen();
}

void SerialPort::onEvents(uint32_t events) {
    if (!(events & (EPOLLIN | EPOLLHUP | EPOLLERR))) return;
    char buf[1024];
    for (;;) {
        const ssize_t n = read(fd_, buf, sizeof buf);
        if (n > 0) {
            if (onData) onData(std::string_view(buf, static_cast<size_t>(n)));
            if (fd_ < 0) return;  // onData may have dropped us
            continue;
        }
        if (n == 0) {
            drop("closed");
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        if (errno == EINTR) continue;
        drop(std::strerror(errno));
        return;
    }
}

bool SerialPort::write(std::string_view bytes) {
    if (fd_ < 0) return false;
    while (!bytes.empty()) {
        const ssize_t n = ::write(fd_, bytes.data(), bytes.size());
        if (n > 0) {
            bytes.remove_prefix(static_cast<size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            LOG_WARN("%s write to %s failed: %s", name_.c_str(), device_.c_str(), n < 0 ? std::strerror(errno) : "no progress");
            return false;
        }
    }
    return true;
}

void SerialPort::drop(const std::string& why) {
    if (fd_ >= 0) {
        loop_.remove(fd_);
        close(fd_);
        fd_ = -1;
    }
    if (wasReachable_) {
        LOG_WARN("%s %s unavailable (%s); retrying every %.0fs", name_.c_str(), device_.c_str(), why.c_str(), retryS_);
        wasReachable_ = false;
    }
    retryTimer_ = loop_.after(retryS_, [this] { openNow(); });
}

}  // namespace dolo
