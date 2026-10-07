#pragma once

#include <cstdint>

namespace mm2::platform {

// Measures frame times. tick() returns the seconds since the previous tick,
// clamped to `maxDelta` so a debugger pause or window drag does not produce
// a huge simulation step.
class FrameClock {
public:
    FrameClock();
    double tick(double maxDelta = 0.25);
    double elapsed() const; // seconds since construction
    double lastDelta() const { return m_lastDelta; }

private:
    std::uint64_t m_start;
    std::uint64_t m_last;
    double m_lastDelta = 0.0;
};

// Optional frame-rate cap, independent of vsync. A target of 0 disables it.
// Call wait() once per frame, just before presenting.
class FrameLimiter {
public:
    void setTargetFps(double fps);
    double targetFps() const { return m_fps; }
    void wait();

private:
    double m_fps = 0.0;
    std::uint64_t m_next = 0;
};

} // namespace mm2::platform
