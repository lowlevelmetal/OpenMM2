#pragma once

// The shared traffic's simulated cars near a client, in full (OpenMM2,
// protocol 12; docs/multiplayer.md "Shared traffic"): the police cars and
// the traffic cars the host knocked loose (bodies) round the client's car.
// The client simulates them along with its own car from these states, as it
// does the players' cars near it (net::NearCarState), so that its car meets
// them as the host's simulation of it does: with their mass, where they
// will be. A police car's state comes with the controls its driver set,
// averaged over the host's frames since the last state (it runs on them
// until the next); a body needs none.
//
// The states are the host's physics sample of `time`, the time of the
// CarStates it sends the client in the same frame, so the client puts them
// in at the sample that message acknowledges. Each message stays one
// datagram (kTrafficFullBytes); the host sends as many as the cars need,
// unreliably on the State channel (unsequenced: each stands alone).

#include "net/AmbientState.h"
#include "net/PlayerCars.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mm2::net {

inline constexpr std::size_t kMaxTrafficFullCars = 8;  // per message
inline constexpr std::size_t kTrafficFullBytes = 1200; // a message's size at most

// A knocked traffic car's body (aiVehicleActive): its rigid body, what a
// sample hands the next, its four vehWheelCheaps and its phSleep.
struct TrafficBodyState {
    Mat34 matrix; // the body frame (the model origin: aiVehicleActive turns about it)
    Vec3 linearMomentum, angularMomentum, linearVelocity, angularVelocity;
    Vec3 force, torque, lastPush;
    bool contact = false; // the impulses and pushes below are not all zero
    Vec3 linearImpulse, angularImpulse, linearPush, turnForce, framePush;
    struct Wheel {
        float compression = 0.0f, lateral = 0.0f, longitudinal = 0.0f;
    };
    std::array<Wheel, 4> wheels{};
    std::int32_t sleepState = 1; // phys::Sleep::State
    std::int32_t stillUpdates = 0, dormantUpdates = 0;
};

struct TrafficFullCar {
    std::uint16_t id = 0;        // the car's ambient id (AmbientEntity::id)
    std::uint8_t generation = 0; // and generation
    AmbientKind kind = AmbientKind::Police;
    // Police: its simulation, and the controls its driver set (averaged;
    // with the throttle's cap, which vehStuck compares against).
    OwnCarState car;
    float throttle = 0.0f, brake = 0.0f, steering = 0.0f, handBrake = 0.0f;
    float maxThrottle = 1.0f;
    // Traffic: its body.
    TrafficBodyState body;
};

struct TrafficFullMsg {
    static constexpr MsgType kType = MsgType::TrafficFull;
    std::uint32_t time = 0; // session ms of the host's physics sample (its CarStates' time)
    std::vector<TrafficFullCar> cars;
};

template <class S>
bool serialize(S& s, TrafficBodyState& b) {
    s.vec3(b.matrix.m0);
    s.vec3(b.matrix.m1);
    s.vec3(b.matrix.m2);
    s.vec3(b.matrix.m3);
    s.vec3(b.linearMomentum);
    s.vec3(b.angularMomentum);
    s.vec3(b.linearVelocity);
    s.vec3(b.angularVelocity);
    s.vec3(b.force);
    s.vec3(b.torque);
    s.vec3(b.lastPush);
    s.boolean(b.contact);
    if (b.contact) {
        s.vec3(b.linearImpulse);
        s.vec3(b.angularImpulse);
        s.vec3(b.linearPush);
        s.vec3(b.turnForce);
        s.vec3(b.framePush);
    } else if constexpr (S::kReading) {
        b.linearImpulse = b.angularImpulse = b.linearPush = b.turnForce = b.framePush = {};
    }
    for (auto& w : b.wheels) {
        s.f32(w.compression);
        s.f32(w.lateral);
        s.f32(w.longitudinal);
    }
    s.ranged(b.sleepState, 0, 2);
    s.ranged(b.stillUpdates, 0, 1023);
    s.ranged(b.dormantUpdates, 0, 1023);
    if constexpr (S::kReading) {
        // Untrusted: every number finite (f32 refuses the rest) and in range.
        const auto ok = [](const Vec3& v, float r) {
            return std::abs(v.x) <= r && std::abs(v.y) <= r && std::abs(v.z) <= r;
        };
        const Mat34& m = b.matrix;
        bool good = ok(m.m0, 2.0f) && ok(m.m1, 2.0f) && ok(m.m2, 2.0f) && ok(m.m3, kOwnStateMaxCoordinate);
        for (const Vec3* v : {&b.linearMomentum, &b.angularMomentum, &b.linearVelocity, &b.angularVelocity,
                              &b.force, &b.torque, &b.lastPush, &b.linearImpulse, &b.angularImpulse,
                              &b.linearPush, &b.turnForce, &b.framePush})
            good = good && ok(*v, kOwnStateMaxValue);
        for (const auto& w : b.wheels)
            good = good && std::abs(w.compression) <= 100.0f && std::abs(w.lateral) <= 100.0f &&
                   std::abs(w.longitudinal) <= 100.0f;
        if (!good)
            return s.fail();
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, TrafficFullMsg& m) {
    s.u32(m.time);
    auto count = static_cast<std::int32_t>(std::min(m.cars.size(), kMaxTrafficFullCars));
    s.ranged(count, 0, static_cast<std::int32_t>(kMaxTrafficFullCars));
    if constexpr (S::kReading)
        m.cars.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        TrafficFullCar& c = m.cars[static_cast<std::size_t>(i)];
        std::int32_t id = c.id, generation = c.generation;
        s.ranged(id, 0, static_cast<std::int32_t>(kMaxAmbientIds) - 1);
        s.ranged(generation, 0, static_cast<std::int32_t>(kAmbientGenerations) - 1);
        c.id = static_cast<std::uint16_t>(id);
        c.generation = static_cast<std::uint8_t>(generation);
        s.enumeration(c.kind, AmbientKind::Last);
        if (c.kind == AmbientKind::Police) {
            if (!serialize(s, c.car))
                return s.fail();
            s.f32(c.throttle);
            s.f32(c.brake);
            s.f32(c.steering);
            s.f32(c.handBrake);
            s.f32(c.maxThrottle);
            if constexpr (S::kReading) {
                // The controls CarSim::setInputs takes, held to its ranges.
                if (std::abs(c.throttle) > 1.0f || std::abs(c.brake) > 1.0f || std::abs(c.steering) > 1.0f ||
                    std::abs(c.handBrake) > 1.0f || std::abs(c.maxThrottle) > 10.0f)
                    return s.fail();
            }
        } else if (!serialize(s, c.body)) {
            return s.fail();
        }
    }
    return s.ok();
}

} // namespace mm2::net
