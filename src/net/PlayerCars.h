#pragma once

// The players' cars in a network race, simulated by the host (OpenMM2's own
// model, see docs/multiplayer.md "Players' cars"; the maintainer's decision
// replaced MM2's peer-to-peer mmNetObject, where each machine sent its own
// car's position and the others dead-reckoned it).
//
//   PlayerInputMsg  client -> host, every frame that simulated a sample: the
//                   inputs of every sample the host has not acknowledged yet
//                   (the newest kMaxInputFrames), each numbered, so a lost
//                   packet loses nothing; and the commands (resets) the host
//                   has not acknowledged.
//   CarStatesMsg    host -> each client, 20 times a second: the last input
//                   the host applied to that client's car and the car's
//                   state after it (full precision: the client puts its car
//                   back to it and runs its later inputs again), and every
//                   other player's car (VehicleSnapshot, drawn interpolated).
//
// Both travel on the State channel (unreliable, unsequenced).

#include "net/Protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mm2::net {

// --- Inputs ------------------------------------------------------------------------

// What the player's controls were for one sample. The pedals are the bytes
// MM2's replay records (mmReplayManager::Update: steering x 127 in a signed
// byte, the pedals x 255), which is what the car is driven with.
struct CarInputFrame {
    std::int8_t steering = 0;
    std::uint8_t throttle = 0;
    std::uint8_t brake = 0;
    std::uint8_t handbrake = 0;
    std::uint8_t flags = 0;  // CarInputFlags: the state of the car's switches
    std::uint8_t events = 0; // CarInputEvents: keys pressed this sample (applied once)
    // Cops and Robbers' gold on this car (mmMultiCR::FondleCarMass): its
    // mass in kg and the throttle cap (x 255; 255 none).
    std::uint16_t extraMass = 0;
    std::uint8_t throttleCap = 255;

    bool operator==(const CarInputFrame&) const = default;
};

enum CarInputFlags : std::uint8_t {
    kInputHeld = 1 << 0,        // held on the grid (vehCar::SetDrivable(0, 1)) until the start
    kInputFinished = 1 << 1,    // mmPlayer +0x2258: brakes, wheel turned full left
    kInputAutomatic = 1 << 2,   // the gearbox is automatic
    kInputAutoReverse = 1 << 3, // the AUTO REVERSE option (mmInput +0x18c)
    kInputHorn = 1 << 4,
    kInputHeadlights = 1 << 5,
};
enum CarInputEvents : std::uint8_t {
    kInputShiftUp = 1 << 0,
    kInputShiftDown = 1 << 1,
    kInputReverse = 1 << 2, // reverse, or first gear from reverse
};
inline constexpr std::uint8_t kCarInputFlagMask = 0x3f;
inline constexpr std::uint8_t kCarInputEventMask = 0x07;
inline constexpr std::uint16_t kMaxExtraMass = 2000; // kg

// A reset of the car the client's rules asked for at sample `seq`, done by
// the host at that sample too (mmPlayer::Reset and the HitWaterHandlers).
enum class CarCommandKind : std::uint8_t {
    Reset,       // back to its reset position (vehCar::Reset)
    ResetTo,     // a new reset position and rotation, then Reset (a start, a debug start)
    RespawnAt,   // there, the reset position kept (mmGameMulti::HitWaterHandler)
    ClearDamage, // vehCar::ClearDamage (Cops and Robbers' repairs, a damage reset)
    Last = ClearDamage,
};
struct CarCommand {
    std::uint32_t seq = 0;
    CarCommandKind kind = CarCommandKind::Reset;
    // ResetTo: vehCarSim's reset position as it holds it (CenterOfGravity
    // added) and rotation; RespawnAt: the position and rotation it is reset
    // at (as SetResetPos takes them).
    Vec3 position;
    float rotation = 0.0f;

    bool operator==(const CarCommand&) const = default;
};

inline constexpr std::size_t kMaxInputFrames = 48; // 0.8 s of samples
inline constexpr std::size_t kMaxCarCommands = 4;
// Positions and angles a command may carry.
inline constexpr float kMaxCommandCoordinate = 16384.0f;
inline constexpr float kMaxCommandAngle = 64.0f;

struct PlayerInputMsg {
    static constexpr MsgType kType = MsgType::PlayerInput;
    std::uint32_t first = 0; // the number of frames[0] (samples count from 1)
    std::vector<CarInputFrame> frames;
    std::vector<CarCommand> commands;
};

template <class S>
bool serialize(S& s, CarInputFrame& f, const CarInputFrame& previous) {
    // A frame like the one before it (no keys pressed) is one bit.
    bool same = f == previous;
    s.boolean(same);
    if (same) {
        f = previous;
        return s.ok();
    }
    std::uint32_t steer = static_cast<std::uint8_t>(f.steering);
    s.bits(steer, 8);
    f.steering = static_cast<std::int8_t>(static_cast<std::uint8_t>(steer));
    s.u8(f.throttle);
    s.u8(f.brake);
    s.u8(f.handbrake);
    std::uint32_t flags = f.flags & kCarInputFlagMask;
    s.bits(flags, 6);
    f.flags = static_cast<std::uint8_t>(flags);
    std::uint32_t events = f.events & kCarInputEventMask;
    s.bits(events, 3);
    f.events = static_cast<std::uint8_t>(events);
    bool gold = f.extraMass != 0 || f.throttleCap != 255;
    s.boolean(gold);
    if (gold) {
        std::int32_t mass = std::min(f.extraMass, kMaxExtraMass);
        s.ranged(mass, 0, kMaxExtraMass);
        f.extraMass = static_cast<std::uint16_t>(mass);
        s.u8(f.throttleCap);
    } else if constexpr (S::kReading) {
        f.extraMass = 0;
        f.throttleCap = 255;
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, CarCommand& c) {
    s.u32(c.seq);
    s.enumeration(c.kind, CarCommandKind::Last);
    if (c.kind == CarCommandKind::ResetTo || c.kind == CarCommandKind::RespawnAt) {
        s.vec3(c.position);
        s.f32(c.rotation);
        const auto inRange = [](float v, float r) { return v >= -r && v <= r; };
        if (!inRange(c.position.x, kMaxCommandCoordinate) || !inRange(c.position.y, kMaxCommandCoordinate) ||
            !inRange(c.position.z, kMaxCommandCoordinate) || !inRange(c.rotation, kMaxCommandAngle))
            return s.fail();
    } else if constexpr (S::kReading) {
        c.position = {};
        c.rotation = 0.0f;
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, PlayerInputMsg& m) {
    s.u32(m.first);
    auto count = static_cast<std::int32_t>(std::min(m.frames.size(), kMaxInputFrames));
    s.ranged(count, 1, static_cast<std::int32_t>(kMaxInputFrames));
    if constexpr (S::kReading)
        m.frames.resize(static_cast<std::size_t>(count));
    // Numbers wrap after 2^32 samples (two years at 60 a second): refused.
    if (m.first == 0 || m.first > 0xFFFFFFFFu - kMaxInputFrames)
        return s.fail();
    CarInputFrame previous;
    for (std::int32_t i = 0; i < count; ++i) {
        serialize(s, m.frames[static_cast<std::size_t>(i)], previous);
        previous = m.frames[static_cast<std::size_t>(i)];
    }
    auto commands = static_cast<std::int32_t>(std::min(m.commands.size(), kMaxCarCommands));
    s.ranged(commands, 0, static_cast<std::int32_t>(kMaxCarCommands));
    if constexpr (S::kReading)
        m.commands.resize(static_cast<std::size_t>(commands));
    for (std::int32_t i = 0; i < commands; ++i)
        serialize(s, m.commands[static_cast<std::size_t>(i)]);
    return s.ok();
}

// --- The host's states -----------------------------------------------------------------

// A car's simulation as the client needs it to carry on from the host's
// state: the rigid body exactly, and the parts whose state decides the next
// samples (the wheels' spin, springs and tyres, the engine and gearbox, the
// drivetrains, the stuck watcher, the damage, the random stream).
struct OwnCarState {
    Mat34 matrix; // the body (centre of mass) frame
    Vec3 linearMomentum, angularMomentum, linearVelocity, angularVelocity;
    Vec3 lastPush;
    struct Wheel {
        float rotationSpeed = 0, rotation = 0, suspension = 0, suspensionVelocity = 0;
        float tireDispLat = 0, tireDispLong = 0;
    };
    std::array<Wheel, 4> wheels{};
    float engineSpeed = 0, gearChangeTime = 0, prevGearRpm = 0;
    bool changingGear = false;
    std::int32_t gear = 2; // transmission slot, 0 reverse .. 7
    bool automatic = true, gearChanged = false;
    float timeInGear = 0;
    std::array<float, 3> drivetrainSpeed{};
    float damage = 0; // vehCarDamage CurrentDamage
    std::int32_t stuckState = 0;
    bool stuckActive = false;
    float stuckTime = 0;
    Vec3 stuckPosition;
    std::uint32_t random = 1;
    bool swapThrottle = false; // the pedals swapped (automatic reverse)
    bool held = false;         // held on the grid
    std::uint32_t resets = 0;  // the commands the host has carried out (wraps)
};

// Ranges an OwnCarState must lie in (anything else is refused).
inline constexpr float kOwnStateMaxCoordinate = 16384.0f;
inline constexpr float kOwnStateMaxValue = 1.0e7f;

template <class S>
bool serialize(S& s, OwnCarState& o) {
    s.vec3(o.matrix.m0);
    s.vec3(o.matrix.m1);
    s.vec3(o.matrix.m2);
    s.vec3(o.matrix.m3);
    s.vec3(o.linearMomentum);
    s.vec3(o.angularMomentum);
    s.vec3(o.linearVelocity);
    s.vec3(o.angularVelocity);
    s.vec3(o.lastPush);
    for (auto& w : o.wheels) {
        s.f32(w.rotationSpeed);
        s.f32(w.rotation);
        s.f32(w.suspension);
        s.f32(w.suspensionVelocity);
        s.f32(w.tireDispLat);
        s.f32(w.tireDispLong);
    }
    s.f32(o.engineSpeed);
    s.f32(o.gearChangeTime);
    s.f32(o.prevGearRpm);
    s.boolean(o.changingGear);
    s.ranged(o.gear, 0, 7);
    s.boolean(o.automatic);
    s.boolean(o.gearChanged);
    s.f32(o.timeInGear);
    for (float& d : o.drivetrainSpeed)
        s.f32(d);
    s.f32(o.damage);
    s.ranged(o.stuckState, 0, 4);
    s.boolean(o.stuckActive);
    s.f32(o.stuckTime);
    s.vec3(o.stuckPosition);
    s.u32(o.random);
    s.boolean(o.swapThrottle);
    s.boolean(o.held);
    s.u32(o.resets);
    if constexpr (S::kReading) {
        // Untrusted: every number finite (f32 refuses the rest) and in range.
        const auto ok = [](const Vec3& v, float r) {
            return std::abs(v.x) <= r && std::abs(v.y) <= r && std::abs(v.z) <= r;
        };
        const Mat34& m = o.matrix;
        bool good = ok(m.m0, 2.0f) && ok(m.m1, 2.0f) && ok(m.m2, 2.0f) && ok(m.m3, kOwnStateMaxCoordinate) &&
                    ok(o.linearMomentum, kOwnStateMaxValue) && ok(o.angularMomentum, kOwnStateMaxValue) &&
                    ok(o.linearVelocity, kOwnStateMaxValue) && ok(o.angularVelocity, kOwnStateMaxValue) &&
                    ok(o.lastPush, kOwnStateMaxValue) && ok(o.stuckPosition, kOwnStateMaxCoordinate);
        for (const auto& w : o.wheels)
            for (float v : {w.rotationSpeed, w.rotation, w.suspension, w.suspensionVelocity, w.tireDispLat,
                            w.tireDispLong})
                good = good && std::abs(v) <= kOwnStateMaxValue;
        for (float v : {o.engineSpeed, o.gearChangeTime, o.prevGearRpm, o.timeInGear, o.damage, o.stuckTime,
                        o.drivetrainSpeed[0], o.drivetrainSpeed[1], o.drivetrainSpeed[2]})
            good = good && std::abs(v) <= kOwnStateMaxValue;
        if (!good)
            return s.fail();
    }
    return s.ok();
}

struct CarStatesMsg {
    static constexpr MsgType kType = MsgType::CarStates;
    std::uint32_t time = 0; // session ms of the host's sample the states belong to
    // The receiving client's car: the number of the last input the host
    // applied to it (0: none yet), how many of its inputs were waiting
    // (beyond the one applied; negative: the host ran short) and its state.
    std::uint32_t ack = 0;
    std::int32_t waiting = 0;
    bool hasOwn = false;
    OwnCarState own;
    // Every other player's car.
    std::vector<std::pair<std::uint8_t, VehicleSnapshot>> cars;
};
inline constexpr std::int32_t kMaxReportedWaiting = 63;

template <class S>
bool serialize(S& s, CarStatesMsg& m) {
    s.u32(m.time);
    s.u32(m.ack);
    m.waiting = std::clamp(m.waiting, -kMaxReportedWaiting, kMaxReportedWaiting);
    s.ranged(m.waiting, -kMaxReportedWaiting, kMaxReportedWaiting);
    s.boolean(m.hasOwn);
    if (m.hasOwn)
        serialize(s, m.own);
    auto count = static_cast<std::uint32_t>(m.cars.size());
    s.varU32(count);
    if (count > kMaxPlayers)
        return s.fail();
    if constexpr (S::kReading)
        m.cars.resize(count);
    for (auto& [id, state] : m.cars) {
        s.u8(id);
        serialize(s, state);
    }
    return s.ok();
}

} // namespace mm2::net
