#pragma once

#include <cstdint>

namespace mm2::platform {

// MM2's limits on one frame's time step (datTimeManager::Update clamps
// Seconds to ClampMin .. ClampMax after measuring the real frame time; the
// game runs in real time, MainPhase calling datTimeManager::RealTime(0)).
// A frame that took longer than a tenth of a second advances the game by only
// a tenth of a second: hitches slow the game down rather than producing one
// huge step.
inline constexpr float kMinFrameSeconds = 0.0001f;
inline constexpr float kMaxFrameSeconds = 0.1f;

// Measures frame times. tick() returns the seconds since the previous tick,
// clamped to [minDelta, maxDelta] (by default MM2's limits above).
class FrameClock {
public:
    FrameClock();
    double tick(double minDelta = kMinFrameSeconds, double maxDelta = kMaxFrameSeconds);
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
