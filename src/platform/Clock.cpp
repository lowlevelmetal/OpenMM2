#include "platform/Clock.h"

#include "platform/Platform.h"

namespace mm2::platform {

FrameClock::FrameClock() : m_start(nowNs()), m_last(m_start) {}

double FrameClock::tick(double minDelta, double maxDelta) {
    const std::uint64_t now = nowNs();
    const double dt = static_cast<double>(now - m_last) * 1e-9;
    m_last = now;
    // datTimeManager::Update: below ClampMin becomes ClampMin, above ClampMax
    // becomes ClampMax.
    m_lastDelta = dt < minDelta ? minDelta : (dt > maxDelta ? maxDelta : dt);
    return m_lastDelta;
}

double FrameClock::elapsed() const { return static_cast<double>(nowNs() - m_start) * 1e-9; }

void FrameLimiter::setTargetFps(double fps) {
    m_fps = fps > 0.0 ? fps : 0.0;
    m_next = 0;
}

void FrameLimiter::wait() {
    if (m_fps <= 0.0)
        return;
    const auto period = static_cast<std::uint64_t>(1e9 / m_fps);
    const std::uint64_t now = nowNs();
    if (m_next == 0 || now > m_next + period) {
        // First frame, or we fell far behind: resynchronise instead of bursting.
        m_next = now + period;
        return;
    }
    if (now < m_next)
        sleepPrecise(static_cast<double>(m_next - now) * 1e-9);
    m_next += period;
}

} // namespace mm2::platform
