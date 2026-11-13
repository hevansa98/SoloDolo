#include "event_loop.hpp"

#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <system_error>

#include "clock.hpp"

namespace dolo {
namespace {
volatile std::sig_atomic_t g_stopRequested = 0;
void onSignal(int) { g_stopRequested = 1; }
}  // namespace

EventLoop::EventLoop() : epfd_(epoll_create1(EPOLL_CLOEXEC)) {
    if (epfd_ < 0) throw std::system_error(errno, std::generic_category(), "epoll_create1");
}

EventLoop::~EventLoop() { close(epfd_); }

void EventLoop::installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = onSignal;  // no SA_RESTART: epoll_wait must return EINTR
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
}

void EventLoop::add(int fd, uint32_t events, FdCallback cb) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) throw std::system_error(errno, std::generic_category(), "epoll_ctl add");
    handlers_[fd] = std::make_shared<FdCallback>(std::move(cb));
}

void EventLoop::modify(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::remove(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    handlers_.erase(fd);
}

EventLoop::TimerId EventLoop::after(double s, std::function<void()> fn) {
    timers_.push_back({nextTimer_, monotonicS() + s, 0, std::move(fn)});
    return nextTimer_++;
}

EventLoop::TimerId EventLoop::every(double s, std::function<void()> fn) {
    timers_.push_back({nextTimer_, monotonicS() + s, s, std::move(fn)});
    return nextTimer_++;
}

void EventLoop::cancel(TimerId id) {
    std::erase_if(timers_, [id](const Timer& t) { return t.id == id; });
}

void EventLoop::run() {
    running_ = true;
    epoll_event events[64];
    while (running_ && !g_stopRequested) {
        int timeoutMs = -1;
        if (!timers_.empty()) {
            const double next = std::min_element(timers_.begin(), timers_.end(),
                                                 [](const Timer& a, const Timer& b) { return a.due < b.due; })->due;
            timeoutMs = static_cast<int>(std::max(0.0, (next - monotonicS()) * 1000.0) + 0.999);
        }
        const int n = epoll_wait(epfd_, events, 64, timeoutMs);
        if (n < 0 && errno != EINTR) throw std::system_error(errno, std::generic_category(), "epoll_wait");

        for (int i = 0; i < n; ++i) {
            const auto it = handlers_.find(events[i].data.fd);
            if (it == handlers_.end()) continue;  // removed by an earlier callback in this batch
            const auto cb = it->second;           // keep alive even if the callback removes itself
            (*cb)(events[i].events);
        }

        // Collect due timers first: callbacks may add or cancel timers.
        const double now = monotonicS();
        std::vector<TimerId> due;
        for (const auto& t : timers_) if (t.due <= now) due.push_back(t.id);
        for (const TimerId id : due) {
            const auto it = std::find_if(timers_.begin(), timers_.end(), [id](const Timer& t) { return t.id == id; });
            if (it == timers_.end()) continue;  // cancelled by an earlier timer
            auto fn = it->fn;
            if (it->interval > 0) it->due = std::max(it->due + it->interval, now);
            else timers_.erase(it);
            fn();
        }
    }
}

}  // namespace dolo
