#pragma once

// Building blocks shared by the AI drivers of physics cars (opponents and
// police), ported from MM1 (Open1560 game.asm / mmai, GPL-3.0):
//   aiStuck              stuck detection with in-place rotation
//   aiGoalBackup         reversing out after the car's own vehStuck fires
//   aiGoalFollowWayPts:: steering towards the target point, the "cheats"
//     Update / CalcSpeed   (Realism, momentum damping) and turn braking
//   aiGoalStop           braking to a halt
// The obstacle scan stands in for aiGoalFollowWayPts::DetectCollision /
// AvoidCollision / AddToBlockedRange (1200 lines of x87 code not ported
// literally; see docs/ai.md).
//
// The drivers write a phys::CarSim's inputs directly (as MM1's AI wrote
// mmCarSim::Steering / Brakes / Engine.Throttle) and select reverse through
// its transmission; do not also drive the car through phys::ArcadeControls.
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/Course.h"
#include "core/Math.h"

#include <functional>
#include <span>

namespace mm2::phys {
class Body;
class CarSim;
class GroundQuery;
struct Impact;
} // namespace mm2::phys

namespace mm2::ai {

// A car the AI should know about this frame: the player, opponents, police
// and ambient traffic. The game fills one list per frame and passes it to
// every driver.
struct TrackedCar {
    int id = -1;              // unique per car; drivers skip their own id
    Vec3 position;            // model origin (on the ground, centre of the car)
    Vec3 forward{0, 0, -1};   // unit facing direction
    Vec3 velocity;            // m/s
    float halfWidth = 1.0f;   // body half extents (m)
    float halfLength = 2.3f;
    const phys::Body* body = nullptr; // physics body, if simulated (impact matching)
    bool collided = false;    // hit something since the previous frame (aiGoalChase::Collision)
    bool isPlayer = false;
    bool isPolice = false;
    bool suspect = false; // police may pursue it (player, racers)
};

// MM1 tuned its per-frame "cheats" for 30 frames per second; Open1560 scales
// them by the frame time (aiGoalChase CopSpeedBoost etc.): factor^(30 dt),
// with the exponent clamped to [0.01, 2]. Used for every per-frame factor.
float perFrame(float factor, float dt);

// Heading error to a target, as aiGoalFollowWayPts::Update computes it:
// atan2(d . m0, d . -m2) with d = target - position (radians, + = right).
float headingError(const Mat34& m, const Vec3& target);

// Signed speed along the car's facing direction (m/s).
float forwardSpeed(const phys::CarSim& car);

// aiStuck (Open1560 aiStuck.h + game.asm aiStuck::Update/Pegged): after an
// impact the position is watched; a car that stays within PosThresh for
// TimeThresh seconds while "pegged" (throttle above 3/4 of MaxThrottle and
// steering under 0.5) is stuck, and is turned in place (RotAmount rad/s,
// in the steering direction) until it moves MoveThresh away or eases off.
class AiStuck {
public:
    enum State : int { Idle = 0, Watching = 1, Stuck = 2 };
    void reset();
    void impact() { m_impacted = true; }
    void update(phys::CarSim& car, float dt);
    int state() const { return m_state; }

    // Values from the aiStuck constructor (0x3E99999A, 0x3F19999A, 1.0, 1.0).
    float timeThresh = 0.3f;
    float posThresh = 0.6f;
    float moveThresh = 1.0f;
    float rotAmount = 1.0f;

private:
    bool pegged(const phys::CarSim& car) const;

    int m_state = Idle;
    bool m_impacted = false;
    float m_time = 0.0f;
    Vec3 m_lastPos;
};

// aiGoalBackup (Open1560 aiGoalBackup.cpp): reverse with opposite lock until
// the car points at `target` (within 0.1 rad), for at most 3 s (5 s when
// moving faster than 2 m/s), then brake to a stop. MM1 aimed at the nearest
// vertex of the road; drivers pass a point a little ahead on their line
// (inferred).
class BackupGoal {
public:
    void start(phys::CarSim& car);
    // Returns false once backing up is over (car stopped, reverse released).
    bool update(phys::CarSim& car, const Vec3& target, float dt);
    bool active() const { return m_active; }
    void cancel() { m_active = false; }

private:
    bool m_active = false;
    float m_time = 0.0f;
};

// Obstacles ahead on the driving line as blocked lateral ranges
// (aiGoalFollowWayPts::AddToBlockedRange idea, inferred details).
struct ObstacleScan {
    struct Range {
        float lo, hi;     // lateral, + right of the line
        float along;      // distance ahead (m)
        float speed;      // obstacle speed along the line (m/s)
        int id;
    };
    std::vector<Range> blocked;
    // Lateral position nearest `want` that is free, inside [minSide, maxSide];
    // returns false when every position is blocked.
    bool freeSide(float want, float minSide, float maxSide, float& out) const;
    // Nearest obstacle covering `side`, or nullptr.
    const Range* blocking(float side) const;
};

struct ScanInput {
    Vec3 position;     // our car
    Vec3 lineDir;      // course direction at our arc length (unit, XZ)
    float lateral = 0; // our offset from the line
    float speed = 0;   // our speed along the line
    float halfWidth = 1.0f;
    int selfId = -1;
    float range = 40.0f; // look-ahead distance
};
ObstacleScan scanObstacles(const ScanInput& in, std::span<const TrackedCar> cars);

// aiGoalFollowWayPts::CalcSpeed for the next turns of a course (decoding in
// Driving.cpp). Returns the brake fraction the turn ahead demands (> 0.7
// means brake), and the corner speed limit in `vmax`. MM1 divides the speed
// to lose by 23.76 m/s^2 (the cornering constant) times the time left;
// `brakeDecel` replaces that divisor with the deceleration the car really
// achieves (see BrakeMeter).
float turnBrake(const Course& course, float s, float side, float speed, float lateralAccel, float brakeDecel,
                float* vmax = nullptr);

// Running estimate of how hard a car decelerates with the brakes on
// (OpenMM2, inferred adaptation): the opponents' "_opp" tunes are MM1-format
// files whose brakes are several times weaker in the current physics than
// the MM2 player tunes, so MM1's assumption that every car can lose
// 23.76 m/s^2 would send them into corners far too fast.
class BrakeMeter {
public:
    void reset(float initial = 6.0f) {
        m_decel = initial;
        m_lastSpeed = -1.0f;
    }
    // Call once per AI frame with the inputs applied last frame.
    void update(const phys::CarSim& car, float dt);
    float decel() const { return m_decel; }

private:
    float m_decel = 6.0f;
    float m_lastSpeed = -1.0f;
    bool m_braking = false;
};

// Rotates a car about the vertical axis through its centre of gravity.
void yawInPlace(phys::CarSim& car, float angle);

// Puts a car back on a course at arc length `s`, facing along it, at the
// place across the road (`side` preferred) farthest from the other cars
// (OpenMM2 recovery when an AI car has made no progress for a long time, or
// fell off the world; inferred). Keeps the car's damage.
void placeOnCourse(phys::CarSim& car, const Course& course, float s, float side, std::span<const TrackedCar> others,
                   int selfId, const std::function<void(const Mat34&)>& resetCar = {},
                   const phys::GroundQuery* world = nullptr);

// True when the straight line from `a` to `b`, `height` m above both, hits
// the static level geometry (false without `world`).
bool blocked(const phys::GroundQuery* world, const Vec3& a, const Vec3& b, float height = 1.0f);

} // namespace mm2::ai
