#include "net/ClockSync.h"

#include <algorithm>

namespace mm2::net {

void ClockSync::addSample(std::uint64_t localSend, std::uint32_t hostTime, std::uint64_t localReceive) {
    if (localReceive < localSend)
        return;
    const auto rtt = static_cast<std::uint32_t>(localReceive - localSend);
    const std::int64_t offset =
        static_cast<std::int64_t>(hostTime) + rtt / 2 - static_cast<std::int64_t>(localReceive);
    m_samples.push_back({offset, rtt});
    while (m_samples.size() > m_window)
        m_samples.pop_front();
    const auto best = std::ranges::min_element(m_samples, {}, &Sample::rtt);
    m_offset = best->offset;
    m_rtt = best->rtt;
}

} // namespace mm2::net
