// Single-threaded epoll reactor with timers. Everything in Dolo runs on this one thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dolo {

class EventLoop {
public:
    using FdCallback = std::function<void(uint32_t events)>;  // EPOLLIN / EPOLLOUT / EPOLLERR ...
    using TimerId = int;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void add(int fd, uint32_t events, FdCallback cb);
    void modify(int fd, uint32_t events);
    void remove(int fd);

    TimerId after(double seconds, std::function<void()> fn);
    TimerId every(double seconds, std::function<void()> fn);
    void cancel(TimerId id);

    /// Runs until stop() is called (or SIGINT/SIGTERM if installSignalHandlers() was used).
    void run();
    void stop() { running_ = false; }
    static void installSignalHandlers();

private:
    struct Timer {
        TimerId id;
        double due, interval;  // interval 0 = one-shot
        std::function<void()> fn;
    };
    int epfd_;
    std::atomic<bool> running_{false};
    TimerId nextTimer_ = 1;
    std::unordered_map<int, std::shared_ptr<FdCallback>> handlers_;
    std::vector<Timer> timers_;
};

}  // namespace dolo
