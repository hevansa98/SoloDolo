// Non-blocking socket helpers: a reconnecting line-oriented TCP client and a listener.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "event_loop.hpp"

namespace dolo {

void setNonBlocking(int fd);

/// Creates a non-blocking listening socket. Throws std::runtime_error on failure.
int listenTcp(const std::string& host, int port);

/// Connects to host:port, delivers newline-terminated lines, and reconnects forever after drops.
class TcpClient {
public:
    TcpClient(EventLoop& loop, std::string name, std::string host, int port, double retryS = 5.0);
    ~TcpClient();
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    std::function<void()> onConnect;
    std::function<void(std::string_view line)> onLine;

    void start() { connectNow(); }
    void send(std::string_view data);  // dropped if not connected

private:
    void connectNow();
    void onEvents(uint32_t events);
    void flush();
    void drop(const std::string& why);

    EventLoop& loop_;
    std::string name_, host_;
    int port_;
    double retryS_;
    int fd_ = -1;
    bool connected_ = false;
    bool wasReachable_ = true;  // logs "unavailable" once per outage instead of every retry
    EventLoop::TimerId retryTimer_ = 0;
    std::string in_, out_;
};

}  // namespace dolo
