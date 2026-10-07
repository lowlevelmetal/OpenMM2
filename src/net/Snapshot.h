#pragma once

// Replicated vehicle state and the receive-side interpolation buffer.

#include "core/Math.h"
#include "net/BitStream.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace mm2::net {

struct VehicleControls {
    float steering = 0.0f;  // -1 (full left) .. 1 (full right)
    float throttle = 0.0f;  // 0..1
    float brake = 0.0f;     // 0..1
    float handbrake = 0.0f; // 0..1
    std::int8_t gear = 0;   // -1 reverse, 0 neutral, 1.. forward

    bool operator==(const VehicleControls&) const = default;
};

enum VehicleFlags : std::uint8_t {
    kVehicleHorn = 1 << 0,
    kVehicleSiren = 1 << 1,
    kVehicleHeadlights = 1 << 2,
    kVehicleBrakeLights = 1 << 3,
    kVehicleWrecked = 1 << 4,
    kVehicleOffRoad = 1 << 5,
};

struct VehicleSnapshot {
    std::uint32_t time = 0; // session (host) time in ms at which the state was sampled
    Vec3 position;
    Quat orientation;
    Vec3 linearVelocity;  // m/s, world space
    Vec3 angularVelocity; // rad/s, world space
    VehicleControls controls;
    float damage = 0.0f; // 0 (pristine) .. 1 (wrecked)
    std::uint8_t flags = 0;
};

// Quantization ranges. Positions cover +-16 km at ~2 mm resolution, far more
// than either city needs; velocities cover +-200 m/s (720 km/h).
inline constexpr float kSnapshotPositionRange = 16384.0f;
inline constexpr int kSnapshotPositionBits = 24;
inline constexpr float kSnapshotVelocityRange = 200.0f;
inline constexpr int kSnapshotVelocityBits = 16;
inline constexpr float kSnapshotAngularRange = 64.0f;
inline constexpr int kSnapshotAngularBits = 14;

template <class S>
bool serialize(S& s, VehicleSnapshot& v) {
    s.u32(v.time);
    s.vec3Quantized(v.position, kSnapshotPositionRange, kSnapshotPositionBits);
    s.quat(v.orientation);
    s.vec3Quantized(v.linearVelocity, kSnapshotVelocityRange, kSnapshotVelocityBits);
    s.vec3Quantized(v.angularVelocity, kSnapshotAngularRange, kSnapshotAngularBits);
    s.quantized(v.controls.steering, -1.0f, 1.0f, 8);
    s.quantized(v.controls.throttle, 0.0f, 1.0f, 7);
    s.quantized(v.controls.brake, 0.0f, 1.0f, 7);
    s.quantized(v.controls.handbrake, 0.0f, 1.0f, 4);
    std::int32_t gear = v.controls.gear;
    s.ranged(gear, -1, 14);
    v.controls.gear = static_cast<std::int8_t>(gear);
    s.quantized(v.damage, 0.0f, 1.0f, 10);
    s.u8(v.flags);
    return s.ok();
}

// Time-ordered buffer of snapshots for one remote vehicle. The renderer
// samples it at (session time - interpolation delay) so there is normally a
// snapshot on each side of the sample time; when the newest snapshot is older
// than the sample time the state is extrapolated for a bounded time and then
// held.
class SnapshotBuffer {
public:
    enum class Result { Empty, Interpolated, Extrapolated, Held };

    explicit SnapshotBuffer(std::size_t capacity = 64) : m_capacity(capacity) {}

    // Inserts in time order. Duplicates (same time) replace the old entry;
    // snapshots older than everything buffered once the buffer is full are dropped.
    void push(const VehicleSnapshot& s);
    void clear() { m_snapshots.clear(); }

    // `time` is in session milliseconds (fractional allowed).
    Result sample(double time, VehicleSnapshot& out, double maxExtrapolationMs = 250.0) const;

    const VehicleSnapshot* latest() const { return m_snapshots.empty() ? nullptr : &m_snapshots.back(); }
    std::size_t size() const { return m_snapshots.size(); }

    // Drops snapshots that can no longer be used for interpolation at `time`.
    void prune(double time);

private:
    std::deque<VehicleSnapshot> m_snapshots;
    std::size_t m_capacity;
};

// Exposed for tests.
VehicleSnapshot interpolateSnapshots(const VehicleSnapshot& a, const VehicleSnapshot& b, double time);
VehicleSnapshot extrapolateSnapshot(const VehicleSnapshot& s, double dtSeconds);

} // namespace mm2::net
