#pragma once

// The shared ambient traffic and police of a multiplayer cruise (OpenMM2
// extra, see docs/multiplayer.md "Shared traffic"): the host simulates them
// and sends each client, about 20 times a second on the unreliable Ambient
// channel, the cars near that client's car.
//
// Every message is complete for its client: a car the client knows that is
// missing from a newer message has left (out of range, or back in the
// traffic's pool). Losing or reordering messages therefore never leaves a
// car behind or brings one back. A car's id is the host's slot (the traffic
// pool index, or a police car's place after the pool); its generation
// changes whenever the host reuses the slot for a new car, so the client
// never blends a recycled car from its old place to its new one.
//
// Positions travel relative to the message's origin (whole metres near the
// receiving car), which keeps them at 3 cm resolution in 44 bits. A car on
// its rail moves along its heading (ai::Traffic: velocity = -m2 x speed),
// so only its speed is sent, with how it is changing (its acceleration and
// its rail's curvature, which let a client predict it along its rail); a
// car off its rail (knocked loose, a wreck) and every police car carry their
// full linear and angular velocity. A car
// far from the client may come without its state (13 bits: it is still
// there, the same car); the host sends those cars' state every other
// message.

#include "net/Protocol.h"

#include <array>
#include <cstdint>
#include <vector>

namespace mm2::net {

inline constexpr std::uint32_t kMaxAmbientIds = 512;   // entity ids 0..511
inline constexpr std::uint32_t kMaxAmbientModels = 64; // catalog indices 0..63
inline constexpr std::uint32_t kMaxAmbientPaint = 15;  // paint job 0..15
inline constexpr std::uint32_t kAmbientGenerations = 8;
inline constexpr std::size_t kMaxAmbientPerMessage = 160;
// Offsets from the origin: +-512 m across, +-256 m up and down.
inline constexpr float kAmbientOffsetRange = 512.0f;
inline constexpr float kAmbientHeightRange = 256.0f;
inline constexpr float kAmbientSpeedRange = 64.0f;     // rail cars, m/s
inline constexpr float kAmbientAccelRange = 16.0f;     // rail cars, m/s^2
inline constexpr float kAmbientCurvatureRange = 0.5f;  // rail cars, rad/m (a 2 m radius)
inline constexpr float kAmbientVelocityRange = 96.0f;  // off-rail cars and police, m/s
inline constexpr float kAmbientSpinRange = 32.0f;      // rad/s
inline constexpr float kAmbientMaxRpm = 10000.0f;
// A knocked car's wheels: each drawing position's offset from its pivot,
// +-0.25 m across and along (the tyre's deflection), +-1 m up (the spring).
inline constexpr float kAmbientWheelAcross = 0.25f;
inline constexpr float kAmbientWheelTravel = 1.0f;
// Police target: a player id, or none.
inline constexpr std::uint8_t kAmbientNoTarget = kInvalidPlayerId;

enum class AmbientKind : std::uint8_t { Traffic, Police, Last = Police };

enum AmbientFlags : std::uint8_t {
    kAmbientBrake = 1 << 0,       // tail lights (decelerating or standing; a police car braking)
    kAmbientHorn = 1 << 1,        // honked since the last message
    kAmbientSignalLeft = 1 << 2,  // indicators: left, right, both = hazards
    kAmbientSignalRight = 1 << 3,
    kAmbientOffRail = 1 << 4,     // traffic: knocked off its rail (physical, avoiding, regaining, wreck)
    kAmbientWrecked = 1 << 5,     // traffic: a wreck that never regains its rail; police: out of action
    kAmbientSiren = 1 << 6,       // police: siren on
    kAmbientPursuit = 1 << 7,     // police: in pursuit (aiPoliceOfficer::InPersuit)
};

struct AmbientEntity {
    std::uint16_t id = 0;
    std::uint8_t generation = 0;
    // False: only the id and generation travel (the car is still there; its
    // state comes with a later message). The other fields are then unset.
    bool hasState = true;
    AmbientKind kind = AmbientKind::Traffic;
    std::uint8_t model = 0; // index into the session's model catalog
    std::uint8_t paint = 0; // paint job
    Vec3 position;          // model origin, world space
    Quat orientation;
    float speed = 0.0f;     // rail cars: along the car's forward axis (-m2)
    // Rail cars: the speed's change (m/s^2) and the heading's turn per metre
    // driven (rad/m, positive turning from +z toward +x: a spin about +y).
    float accel = 0.0f;
    float curvature = 0.0f;
    // Rail cars: how fast it really moves over the ground when that is not
    // its speed (ai::Traffic moves a car along its curves by their parameter,
    // so in a turn it covers more or less ground than its speed; a car held
    // at the end of its lane covers none). Unset: its speed.
    bool slips = false;
    float groundSpeed = 0.0f;
    Vec3 velocity;          // off-rail cars and police
    Vec3 angularVelocity;   // off-rail cars and police
    std::uint8_t flags = 0; // AmbientFlags
    // Traffic off its rail with a physics body (aiVehicleActive): WHL0-3 as
    // aiVehicleInstance::Draw draws them, each its drawing position's offset
    // from its pivot in the car's model space (vehWheelCheap: the spring's
    // travel up, the tyre's deflection across and along).
    bool wheels = false;
    std::array<Vec3, 4> wheelOffsets{};
    // Police only.
    std::uint8_t target = kAmbientNoTarget; // the player it chases
    float damage = 0.0f;                    // 0..1
    float rpm = 0.0f;                       // engine
    float throttle = 0.0f;                  // 0..1
    std::int8_t gear = 0;                   // -1 reverse .. 8

    // Whether the full velocities travel (police, or a car off its rail).
    bool fullMotion() const { return kind == AmbientKind::Police || (flags & kAmbientOffRail) != 0; }
};

// Host -> one client.
struct AmbientStateMsg {
    static constexpr MsgType kType = MsgType::AmbientState;
    std::uint32_t time = 0;       // session time the cars were sampled at
    std::uint32_t lightSteps = 0; // the host's traffic light steps (1/30 s) since its AI reset
    std::uint16_t catalog = 0;    // checksum of the host's model catalog
    std::int16_t origin[3] = {0, 0, 0}; // metres; positions are relative to it
    std::vector<AmbientEntity> entities;

    Vec3 originVec() const {
        return {static_cast<float>(origin[0]), static_cast<float>(origin[1]), static_cast<float>(origin[2])};
    }
    void setOrigin(const Vec3& p);
};

template <class S>
bool serializeAmbientEntity(S& s, AmbientEntity& e, const Vec3& origin) {
    std::int32_t id = e.id, generation = e.generation, model = e.model, paint = e.paint;
    s.ranged(id, 0, static_cast<std::int32_t>(kMaxAmbientIds) - 1);
    s.ranged(generation, 0, static_cast<std::int32_t>(kAmbientGenerations) - 1);
    e.id = static_cast<std::uint16_t>(id);
    e.generation = static_cast<std::uint8_t>(generation);
    s.boolean(e.hasState);
    if (!e.hasState)
        return s.ok();
    s.enumeration(e.kind, AmbientKind::Last);
    s.ranged(model, 0, static_cast<std::int32_t>(kMaxAmbientModels) - 1);
    s.ranged(paint, 0, static_cast<std::int32_t>(kMaxAmbientPaint));
    e.model = static_cast<std::uint8_t>(model);
    e.paint = static_cast<std::uint8_t>(paint);
    Vec3 offset = e.position - origin;
    s.quantized(offset.x, -kAmbientOffsetRange, kAmbientOffsetRange, 15);
    s.quantized(offset.y, -kAmbientHeightRange, kAmbientHeightRange, 14);
    s.quantized(offset.z, -kAmbientOffsetRange, kAmbientOffsetRange, 15);
    if constexpr (S::kReading)
        e.position = origin + offset;
    s.quat(e.orientation);
    s.u8(e.flags);
    if (e.fullMotion()) {
        s.vec3Quantized(e.velocity, kAmbientVelocityRange, 12);
        s.vec3Quantized(e.angularVelocity, kAmbientSpinRange, 11);
        if constexpr (S::kReading)
            e.speed = -(e.orientation.toMatrix().m2.dot(e.velocity));
    } else {
        s.quantized(e.speed, -kAmbientSpeedRange, kAmbientSpeedRange, 11);
        // Its acceleration and curvature and its speed over the ground,
        // unless they are 0, 0 and its speed (most cars stand or drive
        // straight on at their speed: 1 bit).
        bool changing = e.accel != 0.0f || e.curvature != 0.0f || e.slips;
        s.boolean(changing);
        if (changing) {
            s.quantized(e.accel, -kAmbientAccelRange, kAmbientAccelRange, 7);
            s.quantized(e.curvature, -kAmbientCurvatureRange, kAmbientCurvatureRange, 9);
            s.boolean(e.slips);
            if (e.slips)
                s.quantized(e.groundSpeed, -kAmbientSpeedRange, kAmbientSpeedRange, 11);
        } else if constexpr (S::kReading) {
            e.accel = e.curvature = 0.0f;
            e.slips = false;
        }
        if constexpr (S::kReading) {
            if (!e.slips)
                e.groundSpeed = e.speed;
            // ai::Traffic's published velocity of a rail car.
            e.velocity = -e.orientation.toMatrix().m2 * e.speed;
            e.angularVelocity = {};
        }
    }
    if (e.kind == AmbientKind::Traffic && (e.flags & kAmbientOffRail) != 0) {
        s.boolean(e.wheels);
        if (e.wheels)
            for (Vec3& w : e.wheelOffsets) {
                s.quantized(w.x, -kAmbientWheelAcross, kAmbientWheelAcross, 6);
                s.quantized(w.y, -kAmbientWheelTravel, kAmbientWheelTravel, 8);
                s.quantized(w.z, -kAmbientWheelAcross, kAmbientWheelAcross, 6);
            }
    } else if constexpr (S::kReading) {
        e.wheels = false;
    }
    if constexpr (S::kReading)
        if (!e.wheels)
            e.wheelOffsets = {};
    if (e.kind == AmbientKind::Police) {
        std::int32_t target = e.target == kAmbientNoTarget ? static_cast<std::int32_t>(kMaxPlayers)
                                                           : std::min<std::int32_t>(e.target, kMaxPlayers);
        s.ranged(target, 0, static_cast<std::int32_t>(kMaxPlayers));
        e.target = target == static_cast<std::int32_t>(kMaxPlayers) ? kAmbientNoTarget
                                                                     : static_cast<std::uint8_t>(target);
        // As a player's car (VehicleSnapshot): the smoke's four levels and
        // the tyre wobble come from it (vehCarDamage::Update).
        s.quantized(e.damage, 0.0f, 1.0f, 10);
        s.quantized(e.rpm, 0.0f, kAmbientMaxRpm, 8);
        s.quantized(e.throttle, 0.0f, 1.0f, 4);
        std::int32_t gear = e.gear;
        s.ranged(gear, -1, 8);
        e.gear = static_cast<std::int8_t>(gear);
    } else if constexpr (S::kReading) {
        e.target = kAmbientNoTarget;
        e.damage = e.rpm = e.throttle = 0.0f;
        e.gear = 0;
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, AmbientStateMsg& m) {
    s.u32(m.time);
    s.u32(m.lightSteps);
    s.u16(m.catalog);
    for (auto& o : m.origin) {
        auto v = static_cast<std::uint16_t>(o);
        s.u16(v);
        o = static_cast<std::int16_t>(v);
    }
    auto count = static_cast<std::uint32_t>(std::min(m.entities.size(), kMaxAmbientPerMessage));
    s.varU32(count);
    if (count > kMaxAmbientPerMessage)
        return s.fail();
    if constexpr (S::kReading)
        m.entities.resize(count);
    const Vec3 origin = m.originVec();
    for (std::uint32_t i = 0; i < count; ++i)
        if (!serializeAmbientEntity(s, m.entities[i], origin))
            return s.fail();
    return s.ok();
}

// Bits one entity takes on the wire (for the host's packet budget).
std::size_t ambientEntityBits(const AmbientEntity& e);

} // namespace mm2::net
