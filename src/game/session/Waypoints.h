#pragma once

// mmWaypoints' rules for one car: which checkpoints it has cleared, its
// target, its laps and when it is done (mmWaypoints::Update, ClearWaypoint,
// the bookkeeping of DisplayHUDMessage, SetCurrentGoals, GetClosestWaypoint,
// CycleCurrentWaypoint, ResetAllTags). Session runs one for the player and
// shows what it reports (sounds, split times, lap times); a network race's
// host runs one for every player's car (game::session::RaceReferee), so both
// apply the same rules.

#include "game/session/Types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mm2::game::session {

// How a car's checkpoints work (mmWaypoints types).
enum class WaypointRule : std::uint8_t {
    None,
    Circuit,        // 1: in order, waypoint 0 completes a lap
    CheckpointRace, // 2: any order, then the finish
    Blitz,          // 3: any order, done when all are cleared
    AnyOrderEnd,    // 4: crash course: any order, the last one must come last
    InOrder,        // 5: crash course: strictly in order
};

// What a hit did, in the order mmWaypoints::Update does it.
struct WaypointStep {
    enum class Kind : std::uint8_t {
        Cleared,         // `index` cleared (DisplayHUDMessage); `count` the waypoints passed after it
        FinishActivated, // a checkpoint race's finish opens
        LapCompleted,    // `index` = the lap just completed (1-based)
        FinalCheckpoint, // the last checkpoint before the finish
        Finished,        // the waypoints are done
    };
    Kind kind{};
    int index = -1;
    int count = 0;
};

class WaypointTracker {
public:
    // mmWaypoints::Reset for these checkpoints ([0] the start), `laps` a
    // circuit's laps; `singleVisible` shows one marker at a time (a crash
    // course event's single checkpoint).
    void reset(std::span<const Checkpoint> checkpoints, WaypointRule rule, int laps, bool singleVisible);

    // The waypoint the car's gate test hits now (mmWaypoints::Update: the
    // first uncleared one in the any-order rules, the target in the in-order
    // ones), or -1 (none, or the waypoints are done or stopped).
    int detect(std::span<const Checkpoint> checkpoints, const Mat34& car, const Vec3& inertiaBox) const;
    // What that hit does (the rest of mmWaypoints::Update), for a car at
    // `position` (the any-order rules aim at the nearest one left). Returns
    // false when the rule takes nothing from it now (a race's finish before
    // the last checkpoint, a waypoint already cleared, not the target).
    bool apply(std::span<const Checkpoint> checkpoints, int index, const Vec3& position,
               std::vector<WaypointStep>& steps);
    // detect, then apply.
    void update(std::span<const Checkpoint> checkpoints, const Mat34& car, const Vec3& inertiaBox,
                std::vector<WaypointStep>& steps);

    // mmWaypoints::SetCurrentGoals, GetClosestWaypoint and
    // CycleCurrentWaypoint (GetNextWaypoint / GetLastWaypoint).
    void setTarget(int index);
    void closestTarget(std::span<const Checkpoint> checkpoints, const Vec3& position);
    void cycleCurrent(bool forward);

    WaypointRule rule() const { return m_rule; }

    std::vector<char> cleared, visible;
    int current = 1;     // target
    int count = 1;       // waypoints passed, including the start
    int lastCleared = 0; // respawn point
    int lap = 0;
    bool finished = false;
    bool stopped = false; // no more hits (race over)
    bool singleVisible = false;

private:
    void markCleared(int index, std::vector<WaypointStep>& steps);

    WaypointRule m_rule = WaypointRule::None;
    int m_laps = 0;
};

} // namespace mm2::game::session
