#pragma once

// Race opponent driving a physics car (phys::CarSim, normally the "_opp"
// tune of its vehicle) along its .opp driving line: MM1's aiVehicleOpponent
// with its three goals, ported from Open1560 (code/midtown/mmai and
// game.asm, GPL-3.0):
//   aiGoalFollowWayPts  race along the waypoints (Update, Context, PlanRoute,
//                       Reset and CalcSpeed ported; the road-map queries
//                       DetermineOppMapComponent / DetectCollision are
//                       replaced by ai::Course and the obstacle scan)
//   aiGoalBackup        reverse out when the car's vehStuck fires
//   aiGoalStop          brake to a halt after the finish
// MM1 has no rubber-banding (no code scales opponents by race position), so
// neither does this.
//
// Per frame, before the physics step:
//   opponent.setHeld(!session.racersReleased() || !session.opponentActive(i));
//   opponent.update(dt, cars);
// and on the session's OpponentFinished event: opponent.finish().
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.

#include "ai/Course.h"
#include "ai/Driving.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace mm2::phys {
class CarSim;
class GroundQuery;
struct Impact;
} // namespace mm2::phys

namespace mm2::ai {

struct OpponentSettings {
    // aimap [Opponent] first number after the path file (0.84 - 1.0 in the
    // retail data; amateur races lower than professional). Read as MM1's
    // OpponentRaceData::MaxThrottle, the only per-opponent float MM1 passes
    // to aiGoalFollowWayPts (inferred). The other numbers are not used.
    float maxThrottle = 1.0f;
    // Circuit laps (0 = point-to-point race). MM1 PlanRoute loops the
    // waypoints until the lap count is reached, then drives to the last row.
    int laps = 0;
    // MM1 aiGoalFollowWayPts::Context: in circuit races a wrecked opponent
    // is repaired after 5 s; elsewhere it stays wrecked.
    bool repairWhenWrecked = false;
    // vpsemi: MM1 skips the obstacle check for semis.
    bool semi = false;
    // CalcSpeed's 23.76 m/s^2 (flt_61BCBC).
    float lateralAccel = 23.76f;
    // OpenMM2: top speed for scripted cars (m/s, 0 = none); not in MM1.
    float speedLimit = 0.0f;
    // OpenMM2 recovery (inferred): no progress for this long puts the car
    // back on its line; 0 disables it.
    float resetAfterSeconds = 10.0f;
    // Used instead of CarSim::reset for teleports (e.g. a SimVehicle with a
    // trailer); receives the model matrix.
    std::function<void(const Mat34&)> resetCar;
    // Static level geometry (the phys::World) for line-of-sight checks: a
    // target point hidden behind a wall or median is pulled closer, and
    // resets avoid walls (OpenMM2, inferred). Optional.
    const phys::GroundQuery* world = nullptr;
};

class Opponent {
public:
    enum class Mode : std::uint8_t {
        Held,      // waiting for the start (or inactive in this lesson)
        Racing,    // aiGoalFollowWayPts
        BackingUp, // aiGoalBackup
        Wrecked,   // damage over the maximum (FollowWayPts::Context)
        Stopped,   // aiGoalStop after the finish
    };

    // `selfId` is the id this car has in the TrackedCar lists.
    Opponent(phys::CarSim& car, Course course, const OpponentSettings& settings, int selfId = -1);
    ~Opponent();
    Opponent(const Opponent&) = delete;
    Opponent& operator=(const Opponent&) = delete;

    // From the session's OpponentSetup: .opp rows and aimap numbers.
    // `world` is OpponentSettings::world.
    static std::unique_ptr<Opponent> create(const RoadNetwork& net, phys::CarSim& car,
                                            std::span<const city::OpponentPoint> path, std::span<const float> params,
                                            int laps, int selfId, std::string* error = nullptr,
                                            const phys::GroundQuery* world = nullptr);

    // Call after placing the car on its grid spot (aiVehicleOpponent::Reset).
    void reset();
    void setHeld(bool held) { m_held = held; }
    void setSpeedLimit(float metresPerSecond) { m_settings.speedLimit = metresPerSecond; }
    // See OpponentSettings::resetCar (e.g. SimVehicle::reset, for trailers).
    void setResetCar(std::function<void(const Mat34&)> reset) { m_settings.resetCar = std::move(reset); }
    // One AI frame: reads the car, writes its inputs. `cars` may include
    // this car (skipped by id).
    void update(float dt, std::span<const TrackedCar> cars);
    // The race is over for this car: brake to a stop (aiGoalStop).
    void finish();
    // Collision report for this car (installed on CarSim::onImpactCallback
    // by the constructor, chaining any callback already set).
    void onImpact(const phys::Impact& impact);

    Mode mode() const { return m_mode; }
    bool finished() const { return m_finished; }
    int lapsDone() const;
    // Metres driven along the course since the start (laps included).
    float progress() const { return m_progress; }
    float courseDistance() const { return m_s; }
    float side() const { return m_side; }
    Vec3 targetPoint() const { return m_target; }
    const Course& course() const { return m_course; }
    const OpponentSettings& settings() const { return m_settings; }
    int resets() const { return m_resets; }
    int backups() const { return m_backups; }

private:
    void trackProgress(float dt);
    void followWayPoints(float dt, std::span<const TrackedCar> cars);
    void stopGoal(float dt);

    phys::CarSim& m_car;
    Course m_course;
    OpponentSettings m_settings;
    int m_selfId = -1;
    std::function<void(const phys::Impact&)> m_prevCallback;

    Mode m_mode = Mode::Held;
    bool m_held = false;
    bool m_finished = false;
    bool m_collidedWithPlayer = false;
    float m_contactTime = 0.0f; // full realism after touching the player
    const phys::Body* m_playerBody = nullptr;
    AiStuck m_stuck;
    BackupGoal m_backup;
    BrakeMeter m_brakeMeter;
    float m_s = 0.0f;        // arc length on the course
    float m_lateral = 0.0f;  // current offset from the line (+ right)
    float m_progress = 0.0f; // unwrapped, from the start
    float m_side = 0.0f;     // DistToSide: lateral position aimed for
    Vec3 m_target;
    bool m_wrecked = false;
    float m_wreckTime = 0.0f;
    float m_bestProgress = 0.0f;
    float m_noProgressTime = 0.0f;
    int m_resets = 0;
    int m_backups = 0;
    int m_resetStreak = 0;
    float m_lastResetProgress = -1e9f;
};

} // namespace mm2::ai
