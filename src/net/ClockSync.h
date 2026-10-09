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
        m_offset = 0.0;
        m_rtt = 0.0;
    }

    // All values in milliseconds: local send time of the request, the host's
    // session time in the reply, local receive time of the reply.
    void addSample(double localSend, double hostTime, double localReceive);

    bool synced() const { return !m_samples.empty(); }
    std::size_t sampleCount() const { return m_samples.size(); }
    // hostTime ~= localTime + offset()
    double offset() const { return m_offset; }
    double rtt() const { return m_rtt; }
    double toHostTime(double localMs) const { return localMs + m_offset; }

private:
    struct Sample {
        double offset;
        double rtt;
    };
    std::deque<Sample> m_samples;
    std::size_t m_window;
    double m_offset = 0.0;
    double m_rtt = 0.0;
};

// The session clock a client shows: the local clock plus an offset that
// follows ClockSync's estimate. A new estimate is not applied at once while a
// race runs: the shown offset slews toward it at `slewRate` ms per ms (the
// clock runs up to that much faster or slower), so the remote cars, which are
// drawn at session times, never jump and the clock never runs backwards.
// Differences above `stepMs`, and every change while `slewing` is off (the
// lobby), are applied at once.
class SlewedClock {
public:
    void reset() { m_valid = false; }
    // Moves the shown offset toward `target` for the local time `localMs`.
    void update(double localMs, double target, bool slewing, double slewRate = 0.05, double stepMs = 250.0);
    bool valid() const { return m_valid; }
    double offset() const { return m_offset; }

private:
    bool m_valid = false;
    double m_offset = 0.0;
    double m_lastLocal = 0.0;
};

} // namespace mm2::net
