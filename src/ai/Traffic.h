#pragma once

// Ambient traffic: vehicles that ride on lane "rails" with simplified
// dynamics. Behaviour ported from MM1 (Open1560 game.asm):
//   aiGoalRandomDrive::Update / SolveVelocity / AvoidCollision /
//     OkayToEnterIntersection   speed control, car following, junctions
//   aiGoalRandomDrive ctor       per-car acceleration, speed excess, spacing
//   aiVehicleSpline ctor/Update  reaction delay, tyre rotation
//   aiRailSet::ComputeXZCurve /  Hermite turn curves through intersections
//     SolveXZCurve
//   aiMap::AdjustAmbients / NumCars  spawning density and placement
// adapted to MM2's two-sided paths (see docs/ai.md for what is inferred).

#include "ai/Random.h"
#include "ai/RoadNetwork.h"
#include "ai/TrafficLights.h"
#include "ai/VehicleData.h"

#include <functional>
#include <string>
#include <vector>

namespace mm2::ai {

// Constants from MM1 (values decoded from game.asm).
inline constexpr float kAmbientCarSpacing = 8.0f; // aiMap::AdjustAmbients -> NumCars divisor (41000000h)
inline constexpr float kAmbientMinSpawnDistance = 50.0f; // flt_61B238 = 2500 (squared)
inline constexpr float kIntersectionReactDist = 25.0f;   // aiGoalRandomDrive ctor / Reset (41C80000h)
inline constexpr float kRestartReactDist = 5.0f;         // SolveVelocity restart (40A00000h)
inline constexpr float kFollowReactDist = 20.0f;         // flt_61BAB0
inline constexpr float kPlayerZoneDistance = 25.0f;      // flt_61BAA4 = 625 (squared)
inline constexpr float kTireRotationWrap = 6.2831855f;   // flt_61B9C4
inline constexpr float kGridlockSeconds = 30.0f;         // inferred: unseen cars stuck this long are recycled

// Turn the car is about to make, for indicator lights.
enum class TurnSignal : std::uint8_t { None, Left, Right };

// One ambient vehicle as the renderer, audio and physics see it.
struct AmbientCar {
    int id = 0;
    const VehicleData* data = nullptr;
    std::string model;         // geometry/<model>.pkg
    std::uint16_t variant = 0; // random; paint job = variant % paint job count
    Mat34 transform;           // ground contact point, facing -Z
    Vec3 velocity;
    float speed = 0.0f;
    float tireRotation = 0.0f; // radians, wraps at 2 pi (spin = distance travelled)
    float steer = 0.0f;        // front wheel angle, radians, positive = left
    bool braking = false;
    TurnSignal signal = TurnSignal::None;
    bool horn = false;     // blocked by the player
    bool physical = false; // handed over to the physics simulation
};

struct TrafficSettings {
    // Ambient density: the AI map's [Density] (roads listed in [Exceptions]
    // use their own value instead) times the menu's traffic density.
    float mapDensity = 0.1f;
    float densityScale = 1.0f;
    int maxCars = 64;            // pool size
    float activeRadius = 250.0f; // paths with a centre point this close to the player are populated
    // Vehicle types and cumulative spawn probabilities ([Ambient Types/Density]).
    std::vector<city::AiAmbientType> types;
};

class Traffic {
public:
    // Called when a rail car is hit and must become physical
    // (MM1 aiVehicleSpline::Impact -> aiVehicleActive). The handler takes over
    // the car; it stays in cars() with physical = true until release().
    using ImpactHandler = std::function<void(AmbientCar&, const Vec3& impulse)>;

    Traffic(const RoadNetwork& network, TrafficLights& lights, std::vector<VehicleData> types,
            const TrafficSettings& settings, std::uint64_t seed);

    // Fixed-step update. `player*` describe the player's car (drives spawning,
    // culling and the avoid-the-player behaviour).
    void step(float dt, const Vec3& playerPos, const Vec3& playerVel, float playerRadius = 2.5f);

    const std::vector<AmbientCar>& cars() const { return m_public; }
    std::size_t activeCount() const;

    void setImpactHandler(ImpactHandler h) { m_onImpact = std::move(h); }
    // Reports a collision of a rail car (index into cars()).
    void impact(int carId, const Vec3& impulse);
    // Removes a physical car (despawned by the game) or returns it to the pool.
    void release(int carId);

    // Diagnostics for tests and tools.
    struct DebugCar {
        int lane = -1;
        int nextLane = -1;
        float s = 0.0f;
        bool turning = false;
        bool entered = false;
        float accel = 0.0f;
        float targetVelocity = 0.0f;
        int reactTicks = 0;
        int totReactTicks = 0;
        int lead = -1;
        float leadDistance = 0.0f;
    };
    DebugCar debug(int carId) const;
    void populateAll(); // fills every path regardless of distance (tests, tools)

private:
    struct Car {
        bool active = false;
        int type = 0;
        std::uint16_t variant = 0;
        int lane = -1;
        float s = 0.0f;       // RoadDist along the lane (centre of the car)
        int nextLane = -1;    // NextLink/NextLane
        bool turning = false; // on the Hermite curve towards nextLane
        float turnS = 0.0f;   // distance along the turn
        float turnLength = 0.0f;
        Vec4 curveX, curveZ; // cubic coefficients (aiRailSet field_60 / field_70)
        float turnY0 = 0.0f, turnY1 = 0.0f;
        bool enterInt = false; // EnterInt: committed to the intersection
        bool stopped = false;  // waited at a stop sign (aiGoalRandomDrive flag 0x12)
        int waitTicket = 0;    // WaitCount: arrival order at a four-way stop
        float speed = 0.0f;    // CurSpeed
        float accel = 0.0f;    // CurAccelFactor
        float targetVelocity = 0.0f;
        float exceedLimit = 0.0f;  // ExheedLimit
        float vehicleAccel = 5.0f; // VehicleAccelFactor
        float separation = 1.0f;   // SeparationDist
        float intersectionReactDist = kIntersectionReactDist;
        float laneRandomness = 0.0f; // lateral offset (aiRailSet ctor)
        float frontBumper = 2.0f, backBumper = 2.0f;
        int totReactTicks = 8;
        int curReactTicks = 0;
        float tireRotation = 0.0f;
        float steerAngle = 0.0f;
        float stillTime = 0.0f; // seconds without moving
        bool deadEnd = false;   // no road leaves the arrival intersection
        TurnSignal signal = TurnSignal::None;
        bool horn = false;
        bool physical = false;
        Mat34 transform;
        Vec3 prevPosition;
    };

    void spawnOnLane(int lane, const Vec3& playerPos, std::vector<int>& freeSlots);
    void despawn(Car& car);
    int pickType();
    bool chooseNextLane(Car& car);
    void startTurn(Car& car);
    float distanceToIntersection(const Car& car) const;
    // Nearest car ahead of `car` on its rail (same lane, or the next lane when
    // close to the end), with the gap between their centres.
    int leadCar(const Car& car, float& distance) const;
    bool okayToEnter(Car& car);
    bool roadCapacity(const Car& car) const;
    bool anyVehiclesComingThisWay(const Car& car) const;
    void avoidCollision(Car& car, float leadSpeed, float leadAccel, float leadBack, bool leadTurning,
                        float d);
    void solveVelocity(Car& car, float dt);
    void advance(Car& car, float dt);
    void solvePosition(Car& car);
    void avoidPlayer(Car& car, const Vec3& playerPos, const Vec3& playerVel, float playerRadius);
    void rebuildLaneLists();
    void updateActivePaths(const Vec3& playerPos);
    void topUp(const Vec3& playerPos);
    void publish();

    const RoadNetwork& m_net;
    TrafficLights& m_lights;
    std::vector<VehicleData> m_types;
    TrafficSettings m_settings;
    Random m_rng;
    std::vector<Car> m_cars;
    std::vector<AmbientCar> m_public;
    std::vector<std::vector<int>> m_laneCars; // per lane, car indices sorted by s descending (leader first)
    std::vector<std::vector<int>> m_turning;  // per intersection, cars on turn curves through it
    std::vector<std::uint8_t> m_pathActive;
    int m_ticketCounter = 0;      // AIMAP+0x72: four-way stop arrival tickets
    float m_exceedCounter = 0.0f; // flt_6A7BD0: cycles 0, 8, 6, 4, 2 m/s across cars
    ImpactHandler m_onImpact;
    bool m_populateAll = false;
    float m_topUpTimer = 0.0f;
    std::vector<int> m_activeLanes;
};

} // namespace mm2::ai
