#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace mm2::net {

// Estimates the offset between the local monotonic clock and the host's
// session clock from request/response pairs (NTP-style: offset =
// hostTime + rtt/2 - receiveTime). Keeps a window of recent samples and
// trusts the one with the lowest round-trip time, which has the smallest
// possible asymmetry error.
class ClockSync {
public:
    explicit ClockSync(std::size_t window = 8) : m_window(window) {}

    void reset() {
        m_samples.clear();
        m_offset = 0;
        m_rtt = 0;
    }

    // All values in milliseconds: local send time of the request, the host's
    // session time in the reply, local receive time of the reply.
    void addSample(std::uint64_t localSend, std::uint32_t hostTime, std::uint64_t localReceive);

    bool synced() const { return !m_samples.empty(); }
    std::size_t sampleCount() const { return m_samples.size(); }
    // hostTime ~= localTime + offset()
    std::int64_t offset() const { return m_offset; }
    std::uint32_t rtt() const { return m_rtt; }
    double toHostTime(double localMs) const { return localMs + static_cast<double>(m_offset); }

private:
    struct Sample {
        std::int64_t offset;
        std::uint32_t rtt;
    };
    std::deque<Sample> m_samples;
    std::size_t m_window;
    std::int64_t m_offset = 0;
    std::uint32_t m_rtt = 0;
};

} // namespace mm2::net
