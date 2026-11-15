// Non-blocking serial port (raw bytes in and out) that reopens the device after errors.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "event_loop.hpp"

namespace dolo {

/// Baud rates SerialPort supports.
bool isSupportedBaud(int baud);

class SerialPort {
public:
    SerialPort(EventLoop& loop, std::string name, std::string device, int baud, double retryS = 5.0);
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    std::function<void()> onOpen;  // every time the device is (re)opened
    std::function<void(std::string_view bytes)> onData;

    void start() { openNow(); }

    /// Writes all of `bytes`. Returns false if the port is not open or the kernel buffer is full; the caller's
    /// messages are tiny (tens of bytes), so a full buffer means something is wrong and the data is dropped.
    bool write(std::string_view bytes);

private:
    void openNow();
    void onEvents(uint32_t events);
    void drop(const std::string& why);

    EventLoop& loop_;
    std::string name_, device_;
    int baud_;
    double retryS_;
    int fd_ = -1;
    bool wasReachable_ = true;  // logs "unavailable" once per outage instead of every retry
    EventLoop::TimerId retryTimer_ = 0;
};

}  // namespace dolo
