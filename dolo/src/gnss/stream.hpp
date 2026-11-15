// Splits a receiver's byte stream into NMEA sentences and UBX frames. u-blox modules interleave both on one
// UART, and UBX payloads can contain any byte (including newlines), so the stream cannot be split on lines.
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "gnss/ubx.hpp"

namespace dolo
{

    class StreamSplitter
    {
    public:
        std::function<void(std::string_view line)> onNmea; // a '$' sentence up to and including its newline
        std::function<void(const ubx::Frame &)> onUbx;     // only frames with a valid checksum

        /// Accepts any chunking of the stream. Bytes that belong to neither format are skipped to resynchronise.
        void feed(std::string_view bytes);

    private:
        std::string buf_;
    };

} // namespace dolo
