// Line-oriented serial port reader (e.g. a GNSS module's NMEA output) that reopens the device after errors.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "event_loop.hpp"

namespace dolo {

/// Baud rates SerialLineReader supports.
bool isSupportedBaud(int baud);

class SerialLineReader {
public:
    SerialLineReader(EventLoop& loop, std::string name, std::string device, int baud, double retryS = 5.0);
    ~SerialLineReader();
    SerialLineReader(const SerialLineReader&) = delete;
    SerialLineReader& operator=(const SerialLineReader&) = delete;

    std::function<void(std::string_view line)> onLine;

    void start() { openNow(); }

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
    std::string in_;
};

}  // namespace dolo
