#pragma once

// Police cars driving physics cars: MM2's aiPoliceOfficer (an
// aiVehiclePhysics, ai::PhysicsDriver) and aiPoliceForce (which cop pursues
// whom), ported from MM2 build 3393:
//   * a cop sits braked at its post until a player comes within 75 m in
//     front of it (within 1.57 rad of its heading; no speeding or line of
//     sight test, aiPoliceOfficer::DetectPerpetrator / Fov), or, while the
//     player is near the cop, an opponent does and wins a dice roll;
//   * at most three cops pursue one suspect and at most three suspects are
//     pursued; the pursuer nearest the suspect, within 25 m, apprehends it
//     (aiPoliceForce::State), the others follow;
//   * following drives the road route to the suspect, aiming 5 m short of
//     it at its speed + (distance - 12.5 m) (FollowPerpetrator);
//     apprehending gets 12 m ahead of the suspect (or beside its tail when
//     behind it) and then holds its heading 3 m/s slower to block it
//     (Block / aiVehiclePhysics::Mirror);
//   * the suspect escapes beyond [CopChaseDistance] (250 m unless the race's
//     .aimap says otherwise); the cop then stops where it is and watches
//     again. A wrecked cop is out of action;
//   * with the throttle full and under 50 m/s a cop's momentum grows 3 % a
//     frame.
//
// Per frame, before the physics step:
//   police.update(dt, cars, world, session.policeActive());
//
// aiPoliceForce was first ported from Open1560 (MM1's aiPoliceForce.cpp).
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta,
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same
// licence.

#include "ai/Course.h"
#include "ai/Driving.h"
#include "ai/Random.h"

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
    static constexpr int kMaxPerps = 3; // aiPoliceForce: three suspects
    static constexpr int kMaxCops = 3;  // of three pursuers each

    PoliceForce();

    void reset();
    bool registerPerp(int cop, int perp);
    bool unregisterCop(int cop, int perp);
    int findPerp(int perp) const;
    bool find(int cop, int perp) const;
    int copsOn(int perp) const;
    // aiPoliceForce::State: 1 (apprehend) when `cop` is the pursuer nearest
    // the suspect and `copDistance` <= 25 m, else 2 (follow); 5 when the
    // suspect is not registered.
    static constexpr int kApprehend = 1, kFollow = 2, kNotPursued = 5;
    int state(int cop, int perp, std::span<const TrackedCar> cars, float copDistance) const;

private:
    int m_numPerps = 0;
    std::array<int, kMaxPerps> m_perps{};
    std::array<int, kMaxPerps> m_numCops{};
    std::array<std::array<int, kMaxCops>, kMaxPerps> m_cops{};
};

struct PoliceSettings {
    float detectRange = 75.0f;     // DetectPerpetrator (5625 = 75^2)
    float fov = 1.57f;             // aiPoliceOfficer::Fov
    float chaseDistance = 250.0f;  // aiRaceData [CopChaseDistance], default 250
    float opponentChance = 0.5f;   // [Police] 8th number: chance to pursue an opponent
    float opponentRange = 50.0f;   // [Police] 9th: ... while the player is this close to the cop
    unsigned behaviours = 15;      // [Police] 7th: apprehend behaviours (0 = follow only)
    std::uint64_t seed = 0x434f50; // frand() stand-in

    // From a [Police] line's numbers after the heading
    // (aiRaceData::aiRaceData, "%s %f %f %f %f %d %d %f %f": an unused int,
    // behaviours, opponent chance, opponent range; defaults 0, 15, 0.5, 50)
    // and the .aimap's [CopChaseDistance].
    static PoliceSettings fromData(std::span<const float> params, std::optional<float> chaseDistance = {});
};

class PoliceCar {
public:
    enum class Mode : std::uint8_t {
        Parked,   // braked, watching for suspects (at its post or where a chase ended)
        Chasing,  // following or apprehending a suspect
        Disabled, // wrecked: out of action
    };

    PoliceCar(const RoadNetwork& net, phys::CarSim& car, const Mat34& post, int selfId,
              const PoliceSettings& settings = {});
    ~PoliceCar();
    PoliceCar(const PoliceCar&) = delete;
    PoliceCar& operator=(const PoliceCar&) = delete;

    // `active` false holds the cop (OpenMM2: sessions without police
    // activity). `los` is unused by MM2's rules (kept for callers).
    void update(float dt, std::span<const TrackedCar> cars, PoliceForce& force, const phys::GroundQuery* los,
                bool active);
    void onImpact(const phys::CarImpact& impact);
    // aiPoliceOfficer::Reset: back to the post (teleported), braked.
    void reset();

    Mode mode() const { return m_mode; }
    bool siren() const { return m_siren; }
    int target() const { return m_target; } // TrackedCar id of the suspect, -1 = none
    bool closingIn() const { return m_pursuit == PoliceForce::kApprehend; }
    bool blocking() const { return m_pursuit == PoliceForce::kApprehend && m_apprehend == kMirror; }
    int selfId() const { return m_selfId; }
    Vec3 targetPoint() const { return m_driver.target(); }
    phys::CarSim& car() { return m_car; }
    const phys::CarSim& car() const { return m_car; }
    const PhysicsDriver& driver() const { return m_driver; }

    // What started the last pursuit.
    enum class Reason : std::uint8_t { None, PlayerInView, OpponentInView };
    Reason lastReason() const { return m_reason; }

    // Rooms with lvlRoomInfo flag 4 (city::waterRooms), indexed by room id;
    // a cop whose car is in one drops out (aiPoliceOfficer::Update). Null or
    // empty: no such rooms. Not owned.
    void setWaterRooms(const std::vector<std::uint8_t>* rooms) { m_waterRooms = rooms; }

private:
    static constexpr int kBlock = 6, kMirror = 7; // aiPoliceOfficer 0x977e
    static constexpr int kOutOfAction = 12;      // aiPoliceOfficer 0x977a after a wreck

    void detect(std::span<const TrackedCar> cars, PoliceForce& force);
    bool inView(const TrackedCar& c) const;
    void acquire(const TrackedCar& c, Reason why);
    void follow(const TrackedCar& perp, float dist);
    void apprehend(const TrackedCar& perp, std::span<const TrackedCar> cars);
    void block(const TrackedCar& perp);
    void escape(PoliceForce& force);
    void routeTo(const Vec3& goal, const Vec3& heading);
    void setRouteParams(float destinationSpeed, float stopShort, float cornerSpeedFactor);
    DriveContext context();

    const RoadNetwork& m_net;
    phys::CarSim& m_car;
    Mat34 m_post;
    int m_selfId = -1;
    PoliceSettings m_settings;
    PhysicsDriver m_driver;
    Random m_random;
    std::function<void(const phys::CarImpact&)> m_prevCallback;

    Mode m_mode = Mode::Parked;
    Reason m_reason = Reason::None;
    const std::vector<std::uint8_t>* m_waterRooms = nullptr;
    int m_pursuit = 0;     // 0 watching, 1 apprehend, 2 follow, 5 not pursued (as 1), 12 out of action
    int m_lastPursuit = -1;
    int m_apprehend = 3;
    bool m_siren = false;
    int m_target = -1;
    int m_lastPerp = -1;          // aiPoliceOfficer 0x9774: the last suspect, kept over escapes and resets
    int m_perpComponent = -1;     // aiMap::MapComponent of the suspect: id (0x97a4)
    int m_perpComponentType = 0;  // and type (0x97a2)
    std::vector<int> m_ignored; // opponents that lost the dice roll (until reset)
    const phys::Body* m_playerBody = nullptr;
    bool m_touchingPlayer = false;

    // The route to the current goal: aiMap::CalcRoute's waypoints every frame,
    // the course along them rebuilt when they change, every second or when
    // the goal moves 15 m.
    std::optional<Course> m_route;
    std::vector<int> m_routeIds;
    Vec3 m_routeGoal;
    float m_routeAge = 0.0f;
    float m_lastLeg = 0.0f;
    float m_s = 0.0f;
    float m_lateral = 0.0f;
    Vec3 m_destination;
    Vec3 m_destinationHeading;
};

// The city's police: the force and its cars.
class PoliceSquad {
public:
    explicit PoliceSquad(const RoadNetwork& net);

    PoliceCar& add(phys::CarSim& car, const Mat34& post, int selfId, const PoliceSettings& settings = {});
    void update(float dt, std::span<const TrackedCar> cars, const phys::GroundQuery* los, bool active);
    void reset();

    std::vector<std::unique_ptr<PoliceCar>>& cars() { return m_cars; }
    const std::vector<std::unique_ptr<PoliceCar>>& cars() const { return m_cars; }
    PoliceForce& force() { return m_force; }
    bool anySiren() const;
    // Rooms with lvlRoomInfo flag 4 (city::waterRooms), for every cop.
    void setWaterRooms(std::vector<std::uint8_t> rooms);

    // aiMap::Init: of `count` [Police] entries the first
    // trunc(count * clamp(density, 0, 1)) are placed. Cruise passes the
    // menu's cop density; the races pass their table's cop count (0 or 1+).
    static std::size_t countForDensity(std::size_t count, float density);

private:
    const RoadNetwork& m_net;
    PoliceForce m_force;
    std::vector<std::unique_ptr<PoliceCar>> m_cars;
    std::vector<std::uint8_t> m_waterRooms;
};

} // namespace mm2::ai
