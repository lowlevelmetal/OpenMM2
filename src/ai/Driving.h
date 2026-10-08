#pragma once

// MM2's driving controller for AI physics cars (race opponents and police),
// aiVehiclePhysics, ported from MM2 build 3393 (function names from its
// linker map; see docs/ai.md for what is verified and what is inferred):
//
//   DriveRoute / Forward / Backup / FinishedBackingUp / Stop / Mirror
//                        the per-frame state machine and control laws
//   CalcRoute / EnumRoutes / CalcRoadTarget / IsTargetBlocked /
//   CalcObstacleAvoidPoints / EnumTargets / ContinueCheck / DetermineBestRoute
//                        the route planner: a chain of target points down the
//                        road, branching round obstacles, the straightest
//                        route chosen
//   CalcSpeed / CalcRoadSpeed
//                        braking for the bends ahead and at the destination
//   aiStuck              turning a car that is stuck in place
//
// MM2 plans on its aiPath road segments; OpenMM2 plans on an ai::Course (the
// same roads joined into one driving line with the curbs along it), so the
// road-window bookkeeping of the original (three aiPath pointers, vertex
// indices, sharp-turn circles) is replaced by arc lengths on the course.
//
// The drivers write a phys::CarSim's inputs directly (as MM2 wrote
// vehCarSim Steering / Brakes / HandBrake / Engine Throttle) and select
// reverse through its transmission; do not also drive the car through
// phys::ArcadeControls.
//
// Parts of this file (aiStuck, the per-frame scaling, the OpenMM2 recovery)
// were first ported from Open1560 (MM1). Open1560 - An Open Source
// Re-Implementation of Midtown Madness 1 Beta, Copyright (C) 2020 Brick.
// GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/Course.h"
#include "core/Math.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace mm2::phys {
class Body;
class CarSim;
class GroundQuery;
struct CarImpact;
} // namespace mm2::phys

namespace mm2::ai {

// A car the AI should know about this frame: the player, opponents, police
// and ambient traffic. The game fills one list per frame and passes it to
// every driver (trackedCar() / trackedAmbient() fill one as MM2's aiVehicle
// interface describes the car).
struct TrackedCar {
    int id = -1;              // unique per car; drivers skip their own id
    // aiVehicle::Position and GetMatrix: a physics car's inertial frame (its
    // vehCarSim ICS, about the centre of gravity), an ambient car's AI
    // matrix (model origin, on the ground).
    Vec3 position;
    Vec3 forward{0, 0, -1};   // -m2 of that matrix
    Vec3 right;               // m0 of that matrix (zero: right of `forward` in XZ)
    Vec3 velocity;            // m/s
    float speed = -1.0f;      // aiVehicle::Speed (vehCarSim Speed); < 0: |velocity|
    float halfWidth = 1.0f;   // body half extents (m)
    float halfLength = 2.3f;
    // aiVehicle::FrontBumperDistance, BackBumperDistance, LSideDistance,
    // RSideDistance: from `position` along the matrix; < 0 uses halfLength
    // or halfWidth.
    float frontBumper = -1.0f;
    float backBumper = -1.0f;
    float leftSide = -1.0f;
    float rightSide = -1.0f;
    // The box of its collision bound (vehCarModel GetBound(0), model space),
    // which aiPoliceOfficer::Block reads.
    Vec3 boundMin, boundMax;
    bool hasBound = false;
    const phys::Body* body = nullptr; // physics body, if simulated (impact matching)
    bool collided = false;    // hit something since the previous frame
    bool isPlayer = false;
    bool isPolice = false;
    bool suspect = false;     // police may pursue it (player, racers)
    bool reversing = false;   // in reverse gear (aiPoliceOfficer::Update looks at the player's)

    // MM2 obstacle classes (aiVehiclePhysics::IsTargetBlocked).
    bool isOpponent() const { return suspect && !isPlayer && !isPolice; }
    bool isAmbient() const { return !suspect && !isPlayer && !isPolice; }

    float currentSpeed() const { return speed >= 0.0f ? speed : velocity.mag(); }
    float front() const { return frontBumper >= 0.0f ? frontBumper : halfLength; }
    float back() const { return backBumper >= 0.0f ? backBumper : halfLength; }
    float leftDistance() const { return leftSide >= 0.0f ? leftSide : halfWidth; }
    float rightDistance() const { return rightSide >= 0.0f ? rightSide : halfWidth; }
    float boundBack() const { return hasBound ? boundMax.z : back(); }
    float boundLeft() const { return hasBound ? -boundMin.x : leftDistance(); }
    Vec3 rightAxis() const;
};

struct AmbientCar;

// A physics car as MM2's AI sees it: its ICS position and frame, its speed
// (vehCarSim Speed) and its bumper and side distances. Players
// (aiVehiclePlayer) use half the car's InertiaBox, which vehCarSim::Init
// copies into Size; AI cars (aiVehiclePhysics::Init) the box of their
// collision bound, measured from the model origin.
TrackedCar trackedCar(const phys::CarSim& car, int id, bool player);
// An ambient car (aiVehicleSpline): its AI matrix and speed, and the bumper
// and side distances aiVehicleSpline::Init takes from its aiVehicleData box
// (centred on CG).
TrackedCar trackedAmbient(const AmbientCar& car, int id);

// MM2 applies its per-frame "cheats" every frame at whatever rate it runs;
// OpenMM2 scales them to the frame time as for a 30 Hz frame:
// factor^(30 dt), with the exponent clamped to [0.01, 2].
float perFrame(float factor, float dt);

// Heading error to a target, as MM2 measures it everywhere but in Backup:
// atan2(d . m0, d . -m2) in XZ with d = target - position (radians,
// + = right).
float headingError(const Mat34& m, const Vec3& target);

// Signed speed along the car's facing direction (m/s).
float forwardSpeed(const phys::CarSim& car);

// The deceleration, in m/s^2, that aiVehiclePhysics::CalcSpeed /
// CalcRoadSpeed assume for cornering and for braking: MM2's AI grip factor
// 1.2 times 19.8. (MM1's aiGoalFollowWayPts used the same 23.76.)
inline constexpr float kAiGripFactor = 1.2f;
inline constexpr float kAiGrip = kAiGripFactor * 19.8f;
// Bends and turns sharper than this (radians) are braked for (MM2's AI
// sharp-turn angle).
inline constexpr float kSharpTurn = 0.7f;

// aiVehiclePhysics::RegisterRoute's per-route settings. Racers take them
// from their .aimap [Opponent] line (aiRaceData), police set them in code.
struct RouteParams {
    float maxThrottle = 1.0f;       // written to the car's MaxThrottle every frame
    float cornerSpeedFactor = 1.0f; // scales every corner speed (0.89 - 2.29 in the data)
    float brakeThreshold = 0.7f;    // brake when (v - vmax) / (a t) exceeds this
    float lookAhead = 50.0f;        // the route is planned this far (m)
    bool avoidTraffic = true;       // steer round ambient vehicles
    bool avoidProps = true;         // steer round unbreakable props (not modelled in OpenMM2)
    bool avoidPlayers = true;       // steer round the players
    bool avoidOpponents = true;     // steer round other racers (after the third waypoint)
    bool preferSidewalk = false;    // DetermineBestRoute: take a route over the sidewalk first
    float destinationSpeed = 0.0f;  // speed wanted at the destination (m/s)
    float stopShort = 0.0f;         // aim this far short of the destination (m)
};

// aiVehiclePhysics::Init sorts its car into types by name (vppanoz 0,
// vpford 1, vpmustang99 2, vppanozgt 3, vpsemi 4, vpcaddie 5, vpbug 6,
// vppolice 7, vpbullet 8, vpbus 9, others none). EnumRoutes lets every type
// but 3, the Panoz GTR-1 (vppanozgt), go over the sidewalk round an obstacle.
bool goesOverSidewalks(std::string_view vehicle);

// vehStuck settings MM2 gives AI cars (aiVehiclePhysics::Init: TimeThresh
// 0.5 s, PosThresh 1 m, no rotation recovery; aiPoliceOfficer::Init raises
// TimeThresh to 0.75 s for police).
void configureAiVehStuck(phys::CarSim& car, float timeThresh);

// aiStuck: when the car's own vehStuck starts watching (after an impact),
// the position is watched; a car that stays within PosThresh for TimeThresh
// seconds while "pegged" (throttle above 3/4 of MaxThrottle and steering
// under 0.5) is stuck, and is turned in place (RotAmount rad/s, in the
// steering direction) until it moves MoveThresh away or eases off.
class AiStuck {
public:
    enum State : int { Idle = 0, Watching = 1, Stuck = 2 };
    void reset();
    // aiVehiclePhysics::InitForward / InitShortcut clear the state only.
    void clearState() { m_state = Idle; }
    void update(phys::CarSim& car, float dt);
    int state() const { return m_state; }

    // aiStuck::aiStuck: TimeThresh 0.3 s, PosThresh 0.6 m, MoveThresh 1 m,
    // RotAmount 1 rad/s.
    float timeThresh = 0.3f;
    float posThresh = 0.6f;
    float moveThresh = 1.0f;
    float rotAmount = 1.0f;

private:
    bool pegged(const phys::CarSim& car) const;

    int m_state = Idle;
    float m_time = 0.0f;
    Vec3 m_lastPos;
};

// One target point of a planned route (aiRouteNode).
struct RouteNode {
    Vec3 pos;                 // target point
    float s = 0.0f;           // course arc length (unwrapped, from the car's)
    float dist = 0.0f;        // path length from the car
    float angle = 0.0f;       // accumulated turning from the car's heading
    int obstacle = -1;        // TrackedCar id this point avoids, -1 = none
    bool offRoad = false;     // on the sidewalk (IsPosOnRoad 2)
    bool noWayAround = false; // blocked, with no gap found (CalcObstacleAvoidPoints state 3)
    bool destination = false; // the destination itself (SetTargetPtToDestination: dist 9999)
};

struct PlannedRoute {
    std::vector<RouteNode> nodes; // [0] = the car
    bool offRoad = false;
    bool blocked = false;
};

// What a driver needs to know about its place on the course this frame.
struct DriveContext {
    const Course* course = nullptr;
    float s = 0.0f;                   // the car's arc length on the course
    float lateral = 0.0f;             // its offset from the line (+ right)
    float remaining = 1e9f;           // course distance left to the destination (1e9: not on the final leg)
    Vec3 destination;                 // RegisterRoute's destination
    Vec3 destinationHeading;          // and the heading wanted there (zero = any)
    bool finalApproach = false;       // past the last waypoint: plan to the destination
    bool repairWhenWrecked = false;   // aiVehiclePhysics::Init's repair flag (circuits)
    bool touchingPlayer = false;      // collided with the player since the last frame
    int waypointsPassed = 0;          // aiVehiclePhysics 0x967a (other racers are avoided after 3)
    bool noSidewalk = false;          // never over the sidewalk round an obstacle (goesOverSidewalks)
};

// aiVehiclePhysics: one AI car's controller.
class PhysicsDriver {
public:
    enum class State : std::uint8_t { Forward, Backup, Shortcut, Stop };

    PhysicsDriver(phys::CarSim& car, int selfId);

    // aiVehiclePhysics::Reset: forward, nothing planned.
    void reset();
    void setState(State s) { m_state = s; }
    State state() const { return m_state; }
    bool wrecked() const { return m_wrecked; }

    RouteParams params;

    // aiVehiclePhysics::DriveRoute: one frame along `ctx.course`.
    void driveRoute(float dt, std::span<const TrackedCar> cars, const DriveContext& ctx);
    // aiVehiclePhysics::Mirror: match `target`'s heading at its speed - 3 m/s.
    void mirror(float dt, const TrackedCar& target);

    const PlannedRoute& route() const { return m_best; }
    Vec3 target() const { return m_target; }
    int backups() const { return m_backups; }
    float throttle() const { return m_throttle; }
    // aiVehiclePhysics::LSideDistance / RSideDistance.
    float leftSide() const { return m_leftSide; }
    float rightSide() const { return m_rightSide; }
    float brake() const { return m_brake; }
    float steering() const { return m_steering; }
    phys::CarSim& car() { return m_car; }

    // aiVehiclePhysics::CalcRoute and its helpers on a course (public for
    // tests): the routes found from the car, and the chosen one.
    void planRoutes(std::span<const TrackedCar> cars, const DriveContext& ctx);
    const std::vector<PlannedRoute>& routes() const { return m_routes; }

private:
    void initForward();
    void forward(float dt, std::span<const TrackedCar> cars, const DriveContext& ctx);
    void initBackup();
    void backup(float dt, const DriveContext& ctx);
    void finishedBackingUp(const DriveContext& ctx);
    void initShortcut();
    void shortcut(float dt, const DriveContext& ctx);
    void stop(const DriveContext& ctx);
    void calcSpeed(float dt, const DriveContext& ctx);
    void calcRoadSpeed(float dt, const DriveContext& ctx);
    void applyBrake(float brake, float dt);
    // Writes the throttle, brakes and steering to the car (MM2 leaves its
    // handbrake alone everywhere but in Forward).
    void apply();
    // aiStuck and vehStuck in Forward and Shortcut; true when they took the
    // controls this frame.
    bool handleStuck(float dt);
    // Forward and Shortcut first: a car the game has made undrivable only
    // revs (true: nothing else this frame).
    bool undrivable();

    void enumRoutes(std::vector<RouteNode>& nodes, std::span<const TrackedCar> cars, const DriveContext& ctx,
                    int depth);
    void finishRoute(const std::vector<RouteNode>& nodes);
    RouteNode roadTarget(const RouteNode& from, const RouteNode* before, const DriveContext& ctx) const;
    const TrackedCar* blocking(const Vec3& from, const Vec3& to, std::span<const TrackedCar> cars,
                               const DriveContext& ctx, float* along) const;
    int roadState(const Vec3& p, const DriveContext& ctx, float hint) const;

    phys::CarSim& m_car;
    int m_selfId = -1;
    // aiVehiclePhysics::Init: the car's bumper and side distances from the
    // box of its collision bound (model space): FrontBumperDistance -min z,
    // BackBumperDistance max z, LSideDistance -min x, RSideDistance max x.
    float m_frontBumper = 2.0f;
    float m_backBumper = 2.0f;
    float m_leftSide = 1.0f;
    float m_rightSide = 1.0f;
    State m_state = State::Forward;
    State m_lastState = State::Stop;
    AiStuck m_stuck;

    float m_throttle = 0.0f; // aiVehiclePhysics 0x9694
    float m_brake = 0.0f;    // 0x9690
    float m_steering = 0.0f; // 0x9698
    Vec3 m_target;           // 0x9668
    int m_backupFrames = 0;
    float m_backupTime = 0.0f;
    int m_backups = 0;
    bool m_wrecked = false;
    float m_wreckTime = 0.0f;

    std::vector<PlannedRoute> m_routes;
    PlannedRoute m_best;
};

// aiVehiclePhysics::CalcRoadTarget on a course: the next route point from
// `from` (`before` the point before it, or null when `from` is the car,
// facing `carForward`), for a car `side` m from its centre to its side.
RouteNode courseTarget(const RouteNode& from, const RouteNode* before, const Vec3& carForward, float side,
                       const RouteParams& params, const DriveContext& ctx);
// The same with the car's own left and right side distances (MM2 moves the
// left curb in by LSideDistance + 1 m and the right one by RSideDistance + 1 m).
RouteNode courseTarget(const RouteNode& from, const RouteNode* before, const Vec3& carForward, float leftSide,
                       float rightSide, const RouteParams& params, const DriveContext& ctx);

// aiVehiclePhysics::CalcRoadSpeed for the bends of a course ahead of `s`:
// the brake fraction the worst of them demands, and its corner speed in
// `vmax`. A bend of deflection d (|d| > 0.7 rad) with room R between the car
// and its inside curb has the radius r = R / (1 - sin((3.14 - |d|) / 2)) and
// the corner speed sqrt(23.76 r) * cornerSpeedFactor (halved when the road
// after it is an alley); the brake is (speed - vmax) / (23.76 t) with t the
// time to the turn-in point r cos h before the bend.
// Turns already entered (past the turn-in point) are not braked for.
float turnBrake(const Course& course, float s, float side, float speed, float cornerSpeedFactor,
                float lookAhead, float* vmax = nullptr);

// Rotates a car about the vertical axis through its centre of gravity.
void yawInPlace(phys::CarSim& car, float angle);

// Puts a car back on a course at arc length `s`, facing along it, at the
// place across the road (`side` preferred) farthest from the other cars
// (OpenMM2 recovery when an AI car has made no progress for a long time, or
// fell off the world; not in MM2). Keeps the car's damage.
void placeOnCourse(phys::CarSim& car, const Course& course, float s, float side, std::span<const TrackedCar> others,
                   int selfId, const std::function<void(const Mat34&)>& resetCar = {},
                   const phys::GroundQuery* world = nullptr);

// True when the straight line from `a` to `b`, `height` m above both, hits
// the static level geometry (false without `world`).
bool blocked(const phys::GroundQuery* world, const Vec3& a, const Vec3& b, float height = 1.0f);

// aiVehicle::IsBlockingTarget: how far along the way from `from` to `to`
// the box of `obstacle` lies in the path of a car `width` wide (-1: clear).
// A corner of the box blocks when it is ahead, within the way plus `extra`,
// within width / 2 + 1 m of the line and within 0.7 rad of it.
float blockingDistance(const TrackedCar& obstacle, const Vec3& from, const Vec3& to, float extra,
                       float width);

// aiVehicle::PreAvoid: the points to pass `obstacle` on the left and on the
// right, seen from `from` looking along `dir`: each corner of its box pushed
// `clearance` sideways, the leftmost and rightmost of the eight.
void avoidPoints(const TrackedCar& obstacle, const Vec3& from, const Vec3& dir, float clearance, Vec3& left,
                 Vec3& right);

} // namespace mm2::ai
