// Minimal leveled logger writing to stderr (journald timestamps it under systemd).
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace dolo {

enum class LogLevel { Debug, Info, Warning, Error };

inline LogLevel& logThreshold() {
    static LogLevel level = LogLevel::Info;
    return level;
}

inline void logf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
inline void logf(LogLevel level, const char* fmt, ...) {
    if (level < logThreshold()) return;
    static const char* names[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
    std::fprintf(stderr, "%s ", names[static_cast<int>(level)]);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

#define LOG_DEBUG(...) ::dolo::logf(::dolo::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::dolo::logf(::dolo::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::dolo::logf(::dolo::LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::dolo::logf(::dolo::LogLevel::Error, __VA_ARGS__)

}  // namespace dolo
