#pragma once

// The shared traffic of a network cruise predicted forward on a client
// (OpenMM2 extra; docs/multiplayer.md "Shared traffic", docs/review/
// multiplayer-desync-traffic.md). MM2's network cruise has no traffic, so
// there is nothing of MM2's to follow; MM2 does predict its network cars
// forward from their last packet (mmNetObject::PositionUpdate), which is
// what this does for the host's traffic and police.
//
// A client's own car is in the present (or, with the host simulating it,
// ahead of the host), while every message from the host is at least a trip
// old. Drawn at the messages' times, a traffic car was 2.6 m (median, at a
// 60 ms one-way trip) behind where the host had it when the client's car
// met it. The client therefore shows each car where it will be at its own
// car's time:
//   * a car on its rail drives on along its heading at its speed and
//     acceleration, turning by its rail's curvature per metre (ai::Traffic
//     moves it along cubic curves whose heading changes smoothly, and its
//     speed by an acceleration each step);
//   * a police car or a knocked car (a physics body on the host) moves on at
//     its velocity, turning with its yaw rate.
// Where a newer message shows the prediction was off, the drawing blends the
// difference away (TrafficClient); the collisions take the newest
// prediction at once.

#include "ai/Traffic.h"
#include "core/Math.h"

#include <span>
#include <unordered_map>

namespace mm2::game {

// How a car on its rail is moving: its speed's change, its heading's turn
// per metre (the sign of a spin about +y: from +z toward +x) and, where it
// is not its speed, its speed over the ground: ai::Traffic moves a car along
// its curves by their parameter, so in a turn it covers 20-30 % more or less
// ground than its speed says, and a car held at the end of its lane covers
// none.
struct RailMotion {
    float accel = 0.0f;     // m/s^2
    float curvature = 0.0f; // rad/m
    bool slips = false;     // groundSpeed is not the speed
    float groundSpeed = 0.0f;
};

// Host: each AI car's acceleration, curvature and speed over the ground from
// its last two AI steps (a backward difference: the AI changes them smoothly
// from step to step).
class RailMotionTracker {
public:
    // After an AI update; `time` is the session time of its last step (ms).
    // Cars whose time has not changed keep their values.
    void update(std::span<const ai::AmbientCar> cars, double time);
    RailMotion motion(int id) const;
    void clear() { m_cars.clear(); }

private:
    struct Last {
        int spawns = -1;
        Vec3 position;
        float heading = 0.0f;
        float speed = 0.0f;
        float ground = 0.0f;
        double time = 0.0;
        RailMotion motion;
        bool seen = false;
    };
    std::unordered_map<int, Last> m_cars;
};

// The heading on the ground plane of a forward direction (atan2(x, z)).
float groundHeading(const Vec3& forward);

struct PredictedPose {
    Mat34 transform; // model origin
    Vec3 velocity;
    float speed = 0.0f; // along -m2
};

// A car on its rail `dt` seconds after a state (`transform`, `speed`):
// along its heading (-m2, with its slope) for the distance its speed over the
// ground and its acceleration give (stopping at 0: a rail car never
// reverses), its heading and its whole frame turned about the vertical by
// curvature x distance. Its velocity is along its heading at its speed
// changed by the acceleration (what its body takes when it is hit,
// aiVehicleActive::Attach).
PredictedPose predictRailCar(const Mat34& transform, float speed, const RailMotion& motion, float dt);
// A physics body `dt` seconds after a state: along its velocity turned by
// its yaw rate (an arc), its frame turned by its spin.
PredictedPose predictBody(const Mat34& transform, const Vec3& velocity, const Vec3& spin, float dt);

} // namespace mm2::game
