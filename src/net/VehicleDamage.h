#pragma once

// A car's visible damage in a network race (OpenMM2's own protocol; see
// docs/multiplayer.md, "Damage").
//
// MM2's position packet (mmNetObject::SetPositionData) carries the car's
// vehCarDamage::CurrentDamage, and the receiving machine sets its copy of
// the car to it (mmNetObject::PositionUpdate), clearing the car's damage
// when it drops from above 1 to 0 (the owner's reset). Every machine then
// simulates the network car itself, so its own collisions paint the dents
// and break the parts there. OpenMM2 draws the other players' cars and the
// shared police kinematically from their snapshots, so the damage level
// still travels in the snapshot (VehicleSnapshot::damage, the police's
// AmbientEntity::damage), and what the owner's vehCarDamage::ApplyImpact
// painted and broke travels as this reliable game event: the receivers
// replay it through the same texel damage code (with the same random state
// and the same point, so the dents come out the same) and take the same
// parts off.
//
// Each car's damage since its last reset is one record: the texel damage
// patches in order (independent of each other: a patch copies the damaged
// texture's texels, so the order they land in does not matter), and the
// parts broken off. An event adds the patches from `first` on and carries
// every part broken since the reset, so a receiver that missed an event
// still converges on the parts and keeps every patch it did get. A new
// `epoch` starts a new record (the car's damage was cleared).

#include "net/AmbientState.h"
#include "net/Protocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mm2::net {

// The game event type (reliable, ordered; the host relays a joiner's with
// its own budget, Session).
inline constexpr std::uint16_t kVehicleDamageEvent = static_cast<std::uint16_t>(GameEventType::Custom) + 32;

// Whose car: the sender's own, or (from the host only) a shared police car
// by its ambient id.
inline constexpr std::uint16_t kDamageOwnCar = static_cast<std::uint16_t>(kMaxAmbientIds);

inline constexpr std::size_t kMaxDamagePatches = 16; // per event
inline constexpr std::size_t kMaxDamageImpacts = 8;  // per event
// Patches in one record (between two resets); later ones are not sent.
inline constexpr std::uint32_t kMaxDamageRecord = 1024;
// The parts a car can lose (game::damagePartIndex): bits of `parts`.
inline constexpr int kDamagePartCount = 20;
// Model-space points: +-8 m (patches at 0.24 mm, impacts at 1.6 cm).
inline constexpr float kDamagePointRange = 8.0f;
inline constexpr int kDamagePatchBits = 16;
inline constexpr int kDamageImpactBits = 12;
// Impact strengths travel as log2(1 + x) over 0..20 (up to about 10^6).
inline constexpr float kDamageLogRange = 20.0f;
inline constexpr float kDamageMaxSpeed = 128.0f; // m/s

// fxTexelDamage::ApplyDamage on the owner's car.
struct DamagePatch {
    std::uint8_t delay = 0; // ms after the event's time
    Vec3 point;             // the impact point, model space
    std::uint32_t seed = 0; // the texel damage random state it started from
};

// vehCarDamage::ApplyImpact's sparks, shards and impact sound on the
// owner's car (damaging impacts only).
struct DamageImpact {
    std::uint8_t delay = 0;
    Vec3 point;          // model space
    Vec3 normal;         // model space, unit (from the other side towards this car)
    float total = 0.0f;  // the impact's running total (sparks, shards)
    float speed = 0.0f;  // the car's speed then, m/s
    float sound = 0.0f;  // AudImpact::Play's strength, 0 for none
    std::uint16_t audioId = 0;
};

struct VehicleDamageEvent {
    std::uint16_t subject = kDamageOwnCar;
    std::uint8_t epoch = 0;  // the car's damage resets so far (wraps)
    std::uint32_t time = 0;  // session ms the entries' delays count from
    std::uint16_t first = 0; // record index of patches[0]
    std::vector<DamagePatch> patches;
    std::uint32_t parts = 0;      // every part broken off since the reset
    std::uint8_t partsDelay = 0;  // ms after `time` the newest of them broke
    std::vector<DamageImpact> impacts;
};

inline float damageLog(float v) { return std::log2(1.0f + std::max(0.0f, v)); }
inline float damageExp(float v) { return std::exp2(v) - 1.0f; }

template <class S>
bool serialize(S& s, VehicleDamageEvent& e) {
    std::int32_t subject = e.subject, first = e.first;
    s.ranged(subject, 0, static_cast<std::int32_t>(kDamageOwnCar));
    e.subject = static_cast<std::uint16_t>(subject);
    s.u8(e.epoch);
    s.u32(e.time);
    s.ranged(first, 0, static_cast<std::int32_t>(kMaxDamageRecord) - 1);
    e.first = static_cast<std::uint16_t>(first);
    auto count = static_cast<std::int32_t>(std::min(e.patches.size(), kMaxDamagePatches));
    s.ranged(count, 0, static_cast<std::int32_t>(kMaxDamagePatches));
    if (static_cast<std::uint32_t>(first + count) > kMaxDamageRecord)
        return s.fail();
    if constexpr (S::kReading)
        e.patches.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        DamagePatch& p = e.patches[static_cast<std::size_t>(i)];
        s.u8(p.delay);
        s.vec3Quantized(p.point, kDamagePointRange, kDamagePatchBits);
        s.u32(p.seed);
    }
    std::uint32_t parts = e.parts & ((1u << kDamagePartCount) - 1u);
    s.bits(parts, kDamagePartCount);
    e.parts = parts;
    s.u8(e.partsDelay);
    count = static_cast<std::int32_t>(std::min(e.impacts.size(), kMaxDamageImpacts));
    s.ranged(count, 0, static_cast<std::int32_t>(kMaxDamageImpacts));
    if constexpr (S::kReading)
        e.impacts.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        DamageImpact& m = e.impacts[static_cast<std::size_t>(i)];
        s.u8(m.delay);
        s.vec3Quantized(m.point, kDamagePointRange, kDamageImpactBits);
        s.vec3Quantized(m.normal, 1.0f, 8);
        float total = damageLog(m.total), sound = damageLog(m.sound);
        s.quantized(total, 0.0f, kDamageLogRange, 10);
        s.quantized(m.speed, 0.0f, kDamageMaxSpeed, 9);
        s.quantized(sound, 0.0f, kDamageLogRange, 10);
        std::int32_t audio = m.audioId;
        s.ranged(audio, 0, 1000);
        if constexpr (S::kReading) {
            m.total = damageExp(total);
            m.sound = damageExp(sound);
            m.audioId = static_cast<std::uint16_t>(audio);
        }
    }
    return s.ok();
}

// The model-space point a patch travels as: the owner paints with it too,
// so both machines run the texel damage on the same numbers.
inline Vec3 quantizeDamagePoint(const Vec3& point) {
    WriteStream w;
    Vec3 p = point;
    w.vec3Quantized(p, kDamagePointRange, kDamagePatchBits);
    const std::vector<std::byte> bytes = w.writer().take();
    ReadStream r(bytes);
    Vec3 out;
    r.vec3Quantized(out, kDamagePointRange, kDamagePatchBits);
    return out;
}

} // namespace mm2::net
