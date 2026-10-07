#pragma once

// Race opponent driving a physics car (phys::CarSim with its vehicle's own
// tune: MM2 builds AI cars with vehCar::Init(<car>) like the player's) along
// its .opp driving line: MM2's aiRouteRacer, an
// aiVehiclePhysics (ai::PhysicsDriver) given its route once
// (aiRouteRacer::DriveRoute -> RegisterRoute):
//   * the .opp rows between the first (the grid place and heading) and the
//     last (the destination) are the waypoint intersections; circuits drive
//     them `laps` times;
//   * the [Opponent] line of the race's .aimap tunes the driver
//     (aiRaceData; see OpponentSettings::fromData);
//   * a car that falls through the world is disabled (it stops), a wrecked
//     car is repaired after 5 s in circuits only.
// MM2 has no rubber-banding: nothing scales the opponents by race position.
//
// Per frame, before the physics step:
//   opponent.setHeld(!session.racersReleased() || !session.opponentActive(i));
//   opponent.update(dt, cars);
// and on the session's OpponentFinished event: opponent.finish().
//
// First ported from Open1560 (MM1's aiVehicleOpponent). Open1560 - An Open
// Source Re-Implementation of Midtown Madness 1 Beta, Copyright (C) 2020
// Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/Course.h"
#include "ai/Driving.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace mm2::phys {
class CarSim;
class GroundQuery;
struct Impact;
} // namespace mm2::phys

namespace mm2::ai {

struct OpponentSettings {
    // aiRouteRacer::DriveRoute's RegisterRoute settings, from the race's
    // .aimap [Opponent] line (see fromData).
    RouteParams route;
    // Circuit laps (0 = point-to-point race).
    int laps = 0;
    // aiVehiclePhysics::Init param_4 (game mode 3, circuits): a wrecked car
    // is repaired after 5 s; elsewhere it stays wrecked.
    bool repairWhenWrecked = false;
    // Never takes the sidewalk round an obstacle (vppanozgt; see
    // goesOverSidewalks).
    bool noSidewalk = false;
    // OpenMM2: top speed for scripted cars (m/s, 0 = none); not in MM2.
    float speedLimit = 0.0f;
    // OpenMM2 recovery (not in MM2): no progress for this long puts the car
    // back on its line; 0 disables it.
    float resetAfterSeconds = 10.0f;
    // Used instead of CarSim::reset for teleports (e.g. a SimVehicle with a
    // trailer); receives the model matrix.
    std::function<void(const Mat34&)> resetCar;
    // Static level geometry (the phys::World) for the OpenMM2 recovery to
    // avoid walls. Optional.
    const phys::GroundQuery* world = nullptr;

    // The numbers after the car and path file of an [Opponent] line
    // (aiRaceData::aiRaceData, "%s %s %f %d %f %f %d %d %d %d %d %f"):
    //   MaxThrottle, (unused flag), look-ahead distance, brake threshold,
    //   avoid traffic, avoid props, avoid players, avoid opponents,
    //   prefer the sidewalk, corner speed factor
    // e.g. "0.86 0 50.0 0.7 1 1 1 1 0 1.0". Missing numbers keep aiRaceData's
    // defaults (1.0, 0, 50.0, 0.7, 1, 1, 1, 1, 0, 1.0).
    static OpponentSettings fromData(std::span<const float> params, int laps);
};

class Opponent {
public:
    enum class Mode : std::uint8_t {
        Held,      // waiting for the start (or inactive in this lesson)
        Racing,    // aiVehiclePhysics::Forward (or Shortcut)
        BackingUp, // aiVehiclePhysics::Backup
        Wrecked,   // damage over the maximum
        Stopped,   // at its destination after the finish, or disabled
    };

    // `selfId` is the id this car has in the TrackedCar lists.
    Opponent(phys::CarSim& car, Course course, const OpponentSettings& settings, int selfId = -1);
    ~Opponent();
    Opponent(const Opponent&) = delete;
    Opponent& operator=(const Opponent&) = delete;

    // From the session's OpponentSetup: .opp rows and aimap numbers.
    // `world` is OpponentSettings::world; `vehicle` the car's name
    // (aiVehiclePhysics::Init's type, see goesOverSidewalks).
    static std::unique_ptr<Opponent> create(const RoadNetwork& net, phys::CarSim& car,
                                            std::span<const city::OpponentPoint> path, std::span<const float> params,
                                            int laps, int selfId, std::string* error = nullptr,
                                            const phys::GroundQuery* world = nullptr,
                                            std::string_view vehicle = {});

    // Call after placing the car on its grid spot (aiRouteRacer::Reset).
    void reset();
    void setHeld(bool held) { m_held = held; }
    void setSpeedLimit(float metresPerSecond) { m_settings.speedLimit = metresPerSecond; }
    // See OpponentSettings::resetCar (e.g. SimVehicle::reset, for trailers).
    void setResetCar(std::function<void(const Mat34&)> reset) { m_settings.resetCar = std::move(reset); }
    // One AI frame: reads the car, writes its inputs. `cars` may include
    // this car (skipped by id).
    void update(float dt, std::span<const TrackedCar> cars);
    // The race is over for this car: it drives on to its destination and
    // stops there.
    void finish();
    // Collision report for this car (installed on CarSim::onImpactCallback
    // by the constructor, chaining any callback already set).
    void onImpact(const phys::Impact& impact);

    Mode mode() const { return m_mode; }
    bool finished() const { return m_finished; }
    bool disabled() const { return m_disabled; }
    int lapsDone() const;
    // Metres driven along the course since the start (laps included).
    float progress() const { return m_progress; }
    float courseDistance() const { return m_s; }
    float side() const { return m_lateral; }
    Vec3 targetPoint() const { return m_driver.target(); }
    const Course& course() const { return m_course; }
    const OpponentSettings& settings() const { return m_settings; }
    const PhysicsDriver& driver() const { return m_driver; }
    int resets() const { return m_resets; }
    int backups() const { return m_driver.backups(); }

private:
    void trackProgress(float dt);
    float remainingDistance() const;
    int waypointsPassed() const;

    phys::CarSim& m_car;
    Course m_course;
    OpponentSettings m_settings;
    int m_selfId = -1;
    PhysicsDriver m_driver;
    std::function<void(const phys::Impact&)> m_prevCallback;

    Mode m_mode = Mode::Held;
    bool m_held = false;
    bool m_finished = false;
    bool m_disabled = false;
    bool m_touchingPlayer = false;
    const phys::Body* m_playerBody = nullptr;
    float m_s = 0.0f;        // arc length on the course
    float m_lateral = 0.0f;  // current offset from the line (+ right)
    float m_progress = 0.0f; // unwrapped, from the start
    float m_lastLeg = 0.0f;  // course distance from the last waypoint to the destination
    std::vector<float> m_waypointProgress; // progress at each waypoint of the first lap
    float m_bestProgress = 0.0f;
    float m_noProgressTime = 0.0f;
    int m_resets = 0;
    int m_resetStreak = 0;
    float m_lastResetProgress = -1e9f;
};

} // namespace mm2::ai
