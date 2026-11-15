#include "gnss/stream.hpp"

namespace dolo
{
    namespace
    {
        constexpr size_t kMaxNmeaBytes = 200; // sentences are at most 82 characters; longer means a lost newline
    }

    void StreamSplitter::feed(std::string_view bytes)
    {
        buf_.append(bytes);
        size_t pos = 0;
        while (pos < buf_.size())
        {
            const auto first = static_cast<uint8_t>(buf_[pos]);
            if (first == '$')
            {
                const size_t nl = buf_.find('\n', pos);
                if (nl == std::string::npos)
                {
                    if (buf_.size() - pos <= kMaxNmeaBytes)
                        break; // wait for the rest of the sentence
                    ++pos;     // no newline in sight: this '$' was noise
                    continue;
                }
                if (onNmea)
                    onNmea(std::string_view(buf_).substr(pos, nl + 1 - pos));
                pos = nl + 1;
            }
            else if (first == ubx::kSync1)
            {
                if (buf_.size() - pos < 2)
                    break;
                if (static_cast<uint8_t>(buf_[pos + 1]) != ubx::kSync2)
                {
                    ++pos;
                    continue;
                }
                if (buf_.size() - pos < ubx::kHeaderBytes)
                    break;
                const size_t len = static_cast<uint8_t>(buf_[pos + 4]) | static_cast<uint8_t>(buf_[pos + 5]) << 8;
                if (len > ubx::kMaxPayloadBytes)
                {
                    ++pos;
                    continue;
                }
                const size_t total = ubx::kOverheadBytes + len;
                if (buf_.size() - pos < total)
                    break;
                ubx::Frame f;
                f.cls = static_cast<uint8_t>(buf_[pos + 2]);
                f.id = static_cast<uint8_t>(buf_[pos + 3]);
                f.payload.assign(buf_, pos + ubx::kHeaderBytes, len);
                const auto [a, b] = ubx::checksum(f.cls, f.id, f.payload);
                if (a != static_cast<uint8_t>(buf_[pos + total - 2]) || b != static_cast<uint8_t>(buf_[pos + total - 1]))
                {
                    ++pos; // corrupt frame (or a coincidental sync pair): look for the next start
                    continue;
                }
                if (onUbx)
                    onUbx(f);
                pos += total;
            }
            else
            {
                ++pos; // line noise or the tail of something we missed
            }
        }
        buf_.erase(0, pos);
    }

} // namespace dolo
