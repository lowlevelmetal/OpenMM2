#pragma once

// Shared types of the race session (rules, HUD, markers).

#include "core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm2::game::session {

// What the session needs to know about the player's car every frame.
struct PlayerState {
    Mat34 transform;          // car model matrix in the world (Angel convention, faces -Z)
    Vec3 velocity;            // m/s
    float speedMph = 0.0f;
    float rpm = 0.0f;
    float maxRpm = 8000.0f;   // engine MaxRPM, scales the tachometer
    int gear = 1;             // -1 reverse, 0 neutral, 1.. forward
    bool automatic = true;
    float throttle = 0.0f;    // accelerator as pressed by the player, 0..1
    float damage01 = 0.0f;    // 0 (new) .. 1 (wrecked)
    bool wrecked = false;
    bool inWater = false;     // under the water plane of a water room (city/<map>.water)
    int vehicleImpacts = 0;   // running count of impacts against other vehicles
    int objectImpacts = 0;    // running count of impacts against props and buildings
};

// The other cars the rules track: race opponents, or the crash course
// target/chaser cars, in the order of Session::opponents().
struct OpponentState {
    Mat34 transform;
    Vec3 velocity;
    float damage01 = 0.0f;
    bool wrecked = false;
};

// A checkpoint gate. Angel stores a position, a heading and a radius per
// waypoint; the gate is the segment position +- radius * (cos h, sin h) in
// the XZ plane (mmWaypoints::CalculateGatePoints), i.e. across the road.
struct Checkpoint {
    Vec3 position;
    float headingDeg = 0.0f; // as stored; driving direction is (sin h, 0, -cos h)
    float radius = 15.0f;
    Vec2 gateA, gateB;       // gate end points (x, z)
    bool finish = false;     // last waypoint of a race
    bool start = false;      // first waypoint (start line / circuit start-finish)
};

// Things that happened during an update, for audio, music and voice.
enum class EventType : std::uint8_t {
    CountdownReady,    // "Ready..." (MM1: AudSound slot 0)
    CountdownSet,      // "Set..."
    CountdownGo,       // "Go!" (racers released)
    FalseStart,        // throttle during the countdown: 5 s penalty
    PenaltyOver,       // player released after the penalty
    CheckpointCleared, // index = checkpoint
    FinishActivated,   // all checkpoints cleared, the finish is open
    LapCompleted,      // index = lap just completed (1-based), value = lap time
    FinalLap,
    FinalCheckpoint,
    TimerWarning,      // value = seconds left (once per second below 10 s)
    PlayerFinished,    // index = position (1-based), value = time
    OpponentFinished,  // index = opponent, value = position
    TimeUp,
    Wrecked,
    HitWater,
    Respawn,           // the player must be placed at Session::respawnTransform()
    LessonEventStarted,// index = crash course event
    LessonPassed,
    LessonFailed,
    SessionOver,       // results can be shown
};

struct Event {
    EventType type{};
    int index = -1;
    float value = 0.0f;
};

// The HUD's message lines (mmHUD::SetMessage / SetMessage2).
struct HudMessage {
    std::string text;
    float timeLeft = 0.0f;
    bool top = false; // upper line (true) or centre line
};

// Background music state the session suggests (audio::MusicState).
enum class MusicHint : std::uint8_t { Racing, Idle, Results };

} // namespace mm2::game::session
