#include "net/ClockSync.h"

#include <algorithm>
#include <cmath>

namespace mm2::net {

void ClockSync::addSample(double localSend, double hostTime, double localReceive) {
    if (localReceive < localSend)
        return;
    const double rtt = localReceive - localSend;
    const double offset = hostTime + rtt * 0.5 - localReceive;
    m_samples.push_back({offset, rtt});
    while (m_samples.size() > m_window)
        m_samples.pop_front();
    const auto best = std::ranges::min_element(m_samples, {}, &Sample::rtt);
    m_offset = best->offset;
    m_rtt = best->rtt;
}

void SlewedClock::update(double localMs, double target, bool slewing, double slewRate, double stepMs) {
    const double elapsed = std::max(0.0, localMs - m_lastLocal);
    m_lastLocal = localMs;
    const double diff = target - m_offset;
    if (!m_valid || !slewing || std::abs(diff) > stepMs) {
        m_offset = target;
        m_valid = true;
        return;
    }
    const double limit = elapsed * slewRate;
    m_offset += std::clamp(diff, -limit, limit);
}

} // namespace mm2::net
