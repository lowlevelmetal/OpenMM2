#include "net/Snapshot.h"

#include <algorithm>
#include <cmath>

namespace mm2::net {

void SnapshotBuffer::push(const VehicleSnapshot& s) {
    auto it = std::ranges::lower_bound(m_snapshots, s.time, {}, &VehicleSnapshot::time);
    if (it != m_snapshots.end() && it->time == s.time) {
        *it = s;
        return;
    }
    if (m_snapshots.size() >= m_capacity) {
        if (it == m_snapshots.begin())
            return; // older than everything we keep
        m_snapshots.pop_front();
        it = std::ranges::lower_bound(m_snapshots, s.time, {}, &VehicleSnapshot::time);
    }
    m_snapshots.insert(it, s);
}

void SnapshotBuffer::prune(double time) {
    // Keep the last snapshot at or before `time` (needed as the left end of
    // the interpolation interval) and everything after it.
    while (m_snapshots.size() >= 2 && static_cast<double>(m_snapshots[1].time) <= time)
        m_snapshots.pop_front();
}

VehicleSnapshot interpolateSnapshots(const VehicleSnapshot& a, const VehicleSnapshot& b, double time) {
    const double span = static_cast<double>(b.time) - static_cast<double>(a.time);
    if (span <= 0.0)
        return b;
    const float t = static_cast<float>(std::clamp((time - a.time) / span, 0.0, 1.0));
    const float dt = static_cast<float>(span / 1000.0);

    // Cubic Hermite on position using the replicated velocities: smooth
    // through direction changes without overshooting at sane snapshot rates.
    const float t2 = t * t, t3 = t2 * t;
    const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
    const float h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;

    VehicleSnapshot out = t < 0.5f ? a : b; // discrete fields from the nearer sample
    out.time = static_cast<std::uint32_t>(std::llround(time));
    out.position = a.position * h00 + a.linearVelocity * (h10 * dt) + b.position * h01 + b.linearVelocity * (h11 * dt);
    out.orientation = Quat::slerp(a.orientation, b.orientation, t);
    out.linearVelocity = lerp(a.linearVelocity, b.linearVelocity, t);
    out.angularVelocity = lerp(a.angularVelocity, b.angularVelocity, t);
    out.controls.steering = lerp(a.controls.steering, b.controls.steering, t);
    out.controls.throttle = lerp(a.controls.throttle, b.controls.throttle, t);
    out.controls.brake = lerp(a.controls.brake, b.controls.brake, t);
    out.controls.handbrake = lerp(a.controls.handbrake, b.controls.handbrake, t);
    out.damage = lerp(a.damage, b.damage, t);
    return out;
}

VehicleSnapshot extrapolateSnapshot(const VehicleSnapshot& s, double dtSeconds) {
    VehicleSnapshot out = s;
    const float dt = static_cast<float>(dtSeconds);
    out.time = s.time + static_cast<std::uint32_t>(std::llround(dtSeconds * 1000.0));
    out.position = s.position + s.linearVelocity * dt;
    const float w = s.angularVelocity.mag();
    if (w > 1e-6f) {
        const Quat delta = Quat::fromAxisAngle(s.angularVelocity * (1.0f / w), w * dt);
        out.orientation = (delta * s.orientation).normalized();
    }
    return out;
}

SnapshotBuffer::Result SnapshotBuffer::sample(double time, VehicleSnapshot& out, double maxExtrapolationMs) const {
    if (m_snapshots.empty())
        return Result::Empty;
    const auto& first = m_snapshots.front();
    if (time < first.time) {
        out = first; // sampling before anything we have: show the oldest state
        return Result::Held;
    }
    const auto& last = m_snapshots.back();
    if (time >= last.time) {
        const double ahead = time - last.time;
        const bool held = ahead > maxExtrapolationMs;
        out = extrapolateSnapshot(last, std::min(ahead, maxExtrapolationMs) / 1000.0);
        return held ? Result::Held : (ahead == 0.0 ? Result::Interpolated : Result::Extrapolated);
    }
    // first.time < time < last.time: find the bracketing pair.
    auto hi = std::ranges::upper_bound(m_snapshots, time, {},
                                       [](const VehicleSnapshot& s) { return static_cast<double>(s.time); });
    const auto& b = *hi;
    const auto& a = *(hi - 1);
    out = interpolateSnapshots(a, b, time);
    return Result::Interpolated;
}

} // namespace mm2::net
