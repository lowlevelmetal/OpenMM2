#pragma once

// Shared types of the race session (rules, HUD, markers).

#include "core/Math.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm2::game::session {

// vehCarSim's default InertiaBox (width, height, length), used when a car's
// tune does not say.
inline constexpr Vec3 kDefaultInertiaBox{2.0f, 1.0f, 3.0f};

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
    bool wrecked = false;     // mmPlayer::IsMaxDamaged: CurrentDamage > MaxDamage
    bool inWater = false;     // under the water plane of a water room (city/<map>.water)
    int vehicleImpacts = 0;   // running count of impacts against other vehicles
    int objectImpacts = 0;    // running count of impacts against props and buildings
    Vec3 inertiaBox = kDefaultInertiaBox; // tune InertiaBox: the checkpoint hit test's car size
    // With the mouse controller, the steering the car was given (mmPlayer
    // +0x2264), which the instrument cluster's steering bar shows.
    std::optional<float> mouseSteer;
};

// The other cars the rules track: race opponents, or the crash course
// target/chaser cars, in the order of Session::opponents(); police cars in
// the order of Session::police().
struct OpponentState {
    Mat34 transform;
    Vec3 velocity;
    float damage01 = 0.0f;
    bool wrecked = false;
    float currentDamage = 0.0f; // vehCarDamage CurrentDamage (crash course "destroy" events)
    // The AI driver reached the end of its route (aiRouteRacer::Finished);
    // MM2 ranks an opponent as finished from this, not from the gates.
    bool finished = false;
    // Police: aiPoliceOfficer::InPersuit (any chase, or wrecked out of action).
    bool pursuing = false;
    Vec3 inertiaBox = kDefaultInertiaBox;
};

// A checkpoint gate. Angel stores a position, a heading and a radius per
// waypoint; the gate is the segment position +- radius * (cos h, sin h) in
// the XZ plane (mmWaypointObject::CalculateGatePoints), i.e. across the road.
struct Checkpoint {
    Vec3 position;
    float headingDeg = 0.0f; // as stored; driving direction is (sin h, 0, -cos h)
    float radius = 15.0f;
    Vec2 gateA, gateB;       // gate end points (x, z)
    bool finish = false;     // last waypoint of a race
    bool start = false;      // first waypoint (start line / circuit start-finish)
    // Column 6 of the point list (mmWaypointObject hit flag): crash course
    // any-order events clear this one by distance (RadiusHit) instead of the gate.
    bool hitByRadius = false;
    // The stand's depth scale (0: the radius). mmWaypoints::InitStatic builds
    // the crash course's stands 15 deep and mmWaypointObject::SetRadius
    // changes only their width, so lesson stands stay 15 deep.
    float standDepth = 0.0f;
};

// Things that happened during an update, for audio, music, voice and the
// race screen.
enum class EventType : std::uint8_t {
    CountdownReady,      // first countdown message (sound "Startracelow")
    CountdownSet,        // second countdown message ("Startracelow")
    CountdownGo,         // "Go!": racers released, clocks running ("Startracehigh")
    WreckPenalty,        // wrecked: the player is held for `value` seconds, then DamageReset
    DamageReset,         // repair the player's car (mmPlayer::ResetDamage)
    CheckpointCleared,   // index = checkpoint
    FinishActivated,     // checkpoint race: all checkpoints cleared, the finish is open
    LapCompleted,        // index = lap just completed (1-based), value = lap time
    FinalLap,
    FinalCheckpoint,
    TimerWarning,        // value = seconds left; index 0 = one beep, 1 = continuous (<= 3 s)
    PlayerFinished,      // index = position (1-based), value = time
    OpponentFinished,    // index = opponent, value = position
    TimeUp,
    Wrecked,
    HitWater,
    Respawn,             // the player must be placed at Session::respawnTransform()
    Restart,             // the race starts over: every car back to its spawn (mmGame::Reset)
    LessonEventStarted,  // index = crash course event
    LessonPassed,
    LessonFailed,
    PlayerDamageLimits,  // value = MaxDamage for the player (MedDamage value / 2, ImpactThreshold 0)
    OpponentDamageLimits,// index = opponent, value = MaxDamage (MedDamage value / 2)
    SessionOver,         // results can be shown
    Sound,               // index = GameSound; value: 0 play once, 1 loop, -1 stop
    Speech,              // index = SpeechCue, value = its argument (the announcer)
    // Multiplayer: the player finished in `value` seconds, or did not finish
    // (value = Session::kNetDnf): tell the other players
    // (mmGameMulti::SendFinishReq / SendFinishAck).
    NetFinished,
    // Multiplayer under the host's authority (SessionOptions::netRules): the
    // host's word took back waypoint `index`, which this machine had shown
    // (the marker shows again; nothing is played). CheckpointCleared's value
    // is 1 for a waypoint the host's word brought (0: as predicted).
    CheckpointTakenBack,
};

// What the modes ask of the announcer (mmRaceSpeech, mmCCSpeech).
enum class SpeechCue : std::uint8_t {
    PreRace,       // mmGame::Reset: mmRaceSpeech / mmCCSpeech::PlayPreRace
    Results,       // value = the player's place: mmGameSingle::UpdateRewards' PlayResults(place, opponents)
    ResultsPoor,   // mmSingleBlitz: finished after the time ran out, PlayResults(10, 10)
    DamagePenalty, // mmSingleBlitz: wrecked
    LessonResults, // value 1 passed, 0 failed: mmSingleStunt::RegisterFinish -> mmCCSpeech::PlayResults
};

// The modes' 2D sounds (the AudSoundBase handles their InitGameObjects load)
// and mmWaypoints' checkpoint sounds.
enum class GameSound : std::uint8_t {
    StartRaceLow,  // "Startracelow": the countdown lines
    StartRaceHigh, // "Startracehigh": "Go"
    EndOfRaceTag,  // "Endofracetag": won
    YouLose,       // "Youlose": lost, time up
    DamageLose,    // "Damgelose": wrecked or in the water (Blitz, checkpoint race, crash course)
    MessageNote,   // "Messagenote": circuit penalty, an opponent finishing
    TimerWarning,  // "Timerwarning": the last 10 s
    Waypoint,      // "Waypoint": a checkpoint cleared
    LastWaypoint,  // "Lastwaypoint": a circuit lap completed
    NetAlert,      // "Carhorn1double": mmHUD::PlayNetAlert (multiplayer messages)
};

inline const char* gameSoundName(GameSound s) {
    switch (s) {
    case GameSound::StartRaceLow: return "Startracelow";
    case GameSound::StartRaceHigh: return "Startracehigh";
    case GameSound::EndOfRaceTag: return "Endofracetag";
    case GameSound::YouLose: return "Youlose";
    case GameSound::DamageLose: return "Damgelose";
    case GameSound::MessageNote: return "Messagenote";
    case GameSound::TimerWarning: return "Timerwarning";
    case GameSound::Waypoint: return "Waypoint";
    case GameSound::LastWaypoint: return "Lastwaypoint";
    case GameSound::NetAlert: return "Carhorn1double";
    }
    return "";
}

// AudSoundBase::SetVolume after each load (Angel volume); mmWaypoints::Init
// sets 0.95 for its two sounds, InitStatic (the crash course) 0.91.
inline float gameSoundVolume(GameSound s, bool crashCourse) {
    switch (s) {
    case GameSound::EndOfRaceTag:
    case GameSound::YouLose:
    case GameSound::DamageLose: return 0.925f;
    case GameSound::Waypoint:
    case GameSound::LastWaypoint: return crashCourse ? 0.91f : 0.95f;
    case GameSound::NetAlert: return 0.85f; // mmHUD::Init
    default: return 0.9f;
    }
}

struct Event {
    EventType type{};
    int index = -1;
    float value = 0.0f;
};

// The HUD message (mmHUD::SetMessage / SetMessage2): one message at a time
// with a duration and a placement flag (the original's third argument: 1 for
// countdowns and finish lines, 0 for "Time's up!", penalties and failures),
// plus an optional second line that SetMessage clears.
struct HudMessage {
    std::string text;
    float timeLeft = 0.0f;
    bool top = false; // placement flag 1
};

// Background music state the session suggests (audio::MusicState).
enum class MusicHint : std::uint8_t { Racing, Idle, Results };

} // namespace mm2::game::session
