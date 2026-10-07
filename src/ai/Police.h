#pragma once

// Police cars driving physics cars: MM1's aiVehiclePolice with aiGoalChase,
// and aiPoliceForce (which cop pursues whom), ported from Open1560
// (code/midtown/mmai and game.asm, GPL-3.0).
//
// Ported rules (aiGoalChase::Context, Fov, Speeding, Collision, Stopped,
// Follow; aiPoliceForce): a cop notices a suspect within 75 m that hits it,
// or that it can see (within 90 degrees of its heading, line of sight 3.5 m
// above both cars) while the suspect is speeding (over 70 mph on a road with
// four lanes a side, else 40 mph), has just collided with something or is off
// the road. At most three cops pursue one suspect and at most three suspects
// are pursued; the nearest pursuer within 25 m closes in. Chasing cops drive
// at the suspect's speed (+10 m/s when more than 20 m away) with the
// CopSpeedBoost / CopSteerBoost / CopBrakeBoost momentum factors.
//
// Inferred (MM2's [Police] numbers and most of the 1000-line chase update are
// not decoded): cops wait parked at their post; they follow the roads to the
// suspect (shortest route) and drive straight at it when it is close and in
// sight; the closing-in cop rams; a suspect more than 150 m away or out of
// sight for 5 s escapes, and the cop drives back to its post.
//
// Per frame, before the physics step:
//   police.update(dt, cars, session.policeActive());
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/Course.h"
#include "ai/Driving.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace mm2::phys {
class CarSim;
class GroundQuery;
struct Impact;
} // namespace mm2::phys

namespace mm2::ai {

// aiPoliceForce: pursuers per suspect. Cars are identified by TrackedCar ids.
class PoliceForce {
public:
    static constexpr int kMaxPerps = 3;  // aiPoliceForce::MaxPerps
    static constexpr int kMaxCops = 16;  // aiPoliceForce::MaxCops (array size)
    explicit PoliceForce(int maxCops = 3); // Open1560's "maxcops" default

    void reset();
    bool registerPerp(int cop, int perp);
    bool unregisterCop(int cop, int perp);
    int findPerp(int perp) const;
    bool find(int cop, int perp) const;
    int copsOn(int perp) const;
    // aiPoliceForce::State: 3 when `cop` is the pursuer nearest the suspect
    // and `copDistance` <= 25 m (close in), else 4 (follow); 9 when the
    // suspect is not registered.
    int state(int cop, int perp, std::span<const TrackedCar> cars, float copDistance) const;

private:
    int m_maxCops = 3;
    int m_numPerps = 0;
    std::array<int, kMaxPerps> m_perps{};
    std::array<int, kMaxPerps> m_numCops{};
    std::array<std::array<int, kMaxCops>, kMaxPerps> m_cops{};
};

struct PoliceSettings {
    float detectRange = 75.0f;     // aiGoalChase::Context (sym_63A3C8 = 75^2)
    float losHeight = 3.5f;        // aiGoalChase::Fov (flt_61BB78)
    bool chaseStoppedPlayer = false; // MM1 also pursues a stopped player in view; MM2 unconfirmed
    float escapeDistance = 150.0f; // inferred
    float lostSightSeconds = 5.0f; // inferred
    float lateralAccel = 23.76f;   // turn braking on the road route (CalcSpeed)
};

class PoliceCar {
public:
    enum class Mode : std::uint8_t {
        Parked,    // waiting at its post
        Chasing,   // aiGoalChase
        Returning, // driving back to its post after the suspect escaped
        Disabled,  // wrecked
    };

    PoliceCar(const RoadNetwork& net, phys::CarSim& car, const Mat34& post, int selfId,
              const PoliceSettings& settings = {});
    ~PoliceCar();
    PoliceCar(const PoliceCar&) = delete;
    PoliceCar& operator=(const PoliceCar&) = delete;

    // `los` answers line-of-sight probes (normally the phys::World).
    void update(float dt, std::span<const TrackedCar> cars, PoliceForce& force, const phys::GroundQuery* los,
                bool active);
    void onImpact(const phys::Impact& impact);
    // Back to the post, parked (e.g. after a reset of the race).
    void reset();

    Mode mode() const { return m_mode; }
    bool siren() const { return m_siren; }
    int target() const { return m_target; } // TrackedCar id of the suspect, -1 = none
    bool closingIn() const { return m_closingIn; }
    int selfId() const { return m_selfId; }
    Vec3 targetPoint() const { return m_aim; }
    phys::CarSim& car() { return m_car; }
    const phys::CarSim& car() const { return m_car; }

    // Why the last pursuit started (for tests and debugging).
    enum class Reason : std::uint8_t { None, HitMe, Speeding, Collision, OffRoad, Stopped };
    Reason lastReason() const { return m_reason; }

private:
    bool lookForSuspects(std::span<const TrackedCar> cars, PoliceForce& force, const phys::GroundQuery* los);
    bool inView(const TrackedCar& c, const phys::GroundQuery* los) const;
    bool lineOfSight(const Vec3& a, const Vec3& b, const phys::GroundQuery* los) const;
    void chase(float dt, const TrackedCar& perp, std::span<const TrackedCar> cars, PoliceForce& force,
               const phys::GroundQuery* los);
    void driveBack(float dt, std::span<const TrackedCar> cars);
    void park();
    void escape(PoliceForce& force);
    // Follows `m_route` towards `goal` at `targetSpeed`; true while driving.
    void followRoute(float dt, float targetSpeed, std::span<const TrackedCar> cars);
    void driveAt(float dt, const Vec3& aim, float targetSpeed, bool ram);
    bool handleStuck(float dt);
    void planRoute(const Vec3& goal);

    const RoadNetwork& m_net;
    phys::CarSim& m_car;
    Mat34 m_post;
    int m_selfId = -1;
    PoliceSettings m_settings;
    std::function<void(const phys::Impact&)> m_prevCallback;

    Mode m_mode = Mode::Parked;
    Reason m_reason = Reason::None;
    bool m_siren = false;
    bool m_closingIn = false;
    int m_target = -1;
    const phys::Body* m_hitBy = nullptr;
    std::optional<Course> m_route;
    Vec3 m_routeGoal;
    float m_routeAge = 0.0f;
    float m_s = 0.0f;
    float m_lateral = 0.0f;
    Vec3 m_aim;
    float m_lostSight = 0.0f;
    float m_slowTime = 0.0f;
    AiStuck m_stuck;
    BackupGoal m_backup;
    BrakeMeter m_brakeMeter;
};

// The city's police: the force and its cars.
class PoliceSquad {
public:
    PoliceSquad(const RoadNetwork& net, int maxCopsPerSuspect = 3);

    PoliceCar& add(phys::CarSim& car, const Mat34& post, int selfId, const PoliceSettings& settings = {});
    void update(float dt, std::span<const TrackedCar> cars, const phys::GroundQuery* los, bool active);
    void reset();

    std::vector<std::unique_ptr<PoliceCar>>& cars() { return m_cars; }
    const std::vector<std::unique_ptr<PoliceCar>>& cars() const { return m_cars; }
    PoliceForce& force() { return m_force; }
    bool anySiren() const;

    // Which of `count` [Police] entries to place for the menu's cop density
    // (0..1): round(count * density) entries spread evenly (inferred).
    static std::vector<std::size_t> pickByDensity(std::size_t count, float density);

private:
    const RoadNetwork& m_net;
    PoliceForce m_force;
    std::vector<std::unique_ptr<PoliceCar>> m_cars;
};

} // namespace mm2::ai
