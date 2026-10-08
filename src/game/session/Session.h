#pragma once

// Race rules for every single-player mode and the race modes of
// multiplayer, following MM2's game classes: mmGame / mmGameSingle (water,
// falling through the city, scoring), mmSingleRoam, mmSingleBlitz,
// mmSingleCircuit, mmSingleRace (checkpoint race), mmSingleStunt (crash
// course), mmMultiRoam / mmMultiBlitz / mmMultiCircuit / mmMultiRace, and
// mmWaypoints for the checkpoints.
//
// Typical use (RaceScreen):
//
//   auto s = Session::create(config, city, vfs, strings, &error);
//   player.reset(s->playerSpawn());           // and opponents at s->opponents()[i].spawn
//   s->start();
//   every frame:
//     s->update(dt, playerState, opponentStates, policeStates);
//     s->playerHold(): undrivable (brakes, neutral) or braked after the finish;
//     if (!s->racersReleased() || !s->opponentActive(i)) hold AI car i;
//     for (auto e : s->takeEvents()) ... (Respawn, Restart, DamageReset, damage limits, audio)
//     if (s->finished()) show results with s->result();

#include "city/CityData.h"
#include "game/RaceConfig.h"
#include "game/Strings.h"
#include "game/session/Gate.h"
#include "game/session/RaceSetup.h"
#include "game/session/Types.h"
#include "vfs/Vfs.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::game::session {

enum class Phase : std::uint8_t {
    Countdown, // lesson introduction, Ready / Set (racers held)
    Racing,
    PostRace,  // finished, failed or wrecked; results follow after a delay
    Done,      // show the results
};

// What the game does to the player's car.
enum class PlayerHold : std::uint8_t {
    None,        // it drives
    Undrivable,  // vehCar::SetDrivable(0, 1): brakes on, gearbox in neutral, the throttle revs
    FinishBrake, // mmPlayer +0x2258: the race is over, brakes on with the wheel turned full left
};

struct SessionOptions {
    float scoringBias = 1.0f; // tune/<car>.info ScoringBias (race score multiplier)
    bool skipCountdown = false;
    std::uint32_t seed = 0;   // random parts (the cruise start); 0 = random
    // Line of sight between two points for mmSingleStunt::CheckCopPursuit
    // (the original probes the level); unset = always clear.
    std::function<bool(const Vec3& from, const Vec3& to)> lineOfSight;
    // The local player's network name: multiplayer races show it over
    // "finished in" (mmMultiBlitz / mmMultiCircuit / mmMultiRace::UpdateGame).
    std::string playerName;
};

// The game's cheat flag (bCheating): mmGame::SendChatMessage's "/blubber"
// sets it, and only mmStatePack::SetDefaults (the game's start) clears it.
// While it is set no finish is registered (the modes' RegisterFinish).
bool cheating();
void setCheating(bool on);

class Session {
public:
    static std::unique_ptr<Session> create(const RaceConfig& config, const city::CityData& city,
                                           const vfs::Vfs& vfs, const Strings& strings,
                                           std::string* error = nullptr, const SessionOptions& options = {});

    const RaceSetup& setup() const { return m_setup; }
    GameMode mode() const { return m_setup.config.mode; }
    bool multiplayer() const { return m_setup.config.multiplayer; }

    // Starting places.
    Mat34 playerSpawn() const { return m_setup.playerSpawn; }
    const std::vector<OpponentSetup>& opponents() const { return m_setup.opponents; }
    const std::vector<PoliceSetup>& police() const { return m_setup.police; }

    void start();
    // Whether the pre-race camera is still blending to the game camera
    // (mmPlayer +0xE5A). Circuit and checkpoint races start their countdown
    // only once it has finished (mmSingleCircuit / mmSingleRace::UpdateGame
    // state 0). Set before update().
    void setPreRaceCamera(bool active) { m_preRaceCamera = active; }
    // Multiplayer races: whether the host's start message has arrived (the
    // countdown of mmMultiBlitz / mmMultiCircuit / mmMultiRace::UpdateGame
    // waits for it in state 0; OpenMM2 sends a shared start time instead and
    // signals 2.5 s before it). Single player ignores it.
    void setStartSignal(bool received) { m_startSignal = received || !multiplayer(); }
    // `opponents` in the order of opponents(); `police` in the order of
    // police() (crash course chasers, cop chase lessons).
    //
    // Expects the cars' physics matrices (vehCarSim's phInertialCS, i.e. at
    // the centre of gravity): every rule of the original tests those.
    void update(float dt, const PlayerState& player, std::span<const OpponentState> opponents = {},
                std::span<const OpponentState> police = {});
    // The whole race starts over (mmGame::Reset, e.g. the pause menu's
    // Restart); emits Restart.
    void restart();

    Phase phase() const { return m_phase; }
    bool finished() const { return m_phase == Phase::Done; }
    // What the game does to the player's car: undrivable before "Go!", during
    // wreck penalties and after some endings (a wreck, any multiplayer
    // finish), braked by mmPlayer +0x2258 after the others.
    PlayerHold playerHold() const;
    bool playerHeld() const { return playerHold() == PlayerHold::Undrivable; }
    // A single-player race or Blitz lost to a wreck ("damaged out"): the
    // music stops at once and the engine falls silent
    // (vehCarAudioContainer::SilenceEngine), as after the water in a race.
    bool damagedOut() const { return m_damagedOut; }
    bool engineSilenced() const { return m_engineSilenced; }
    // AI racers may drive (mmGameSingle::EnableRacers at "Go!").
    bool racersReleased() const { return m_released; }
    // Opponents taking part (all in races; the current crash course event's
    // "numopp" cars, mmSingleStunt::EnableRacers).
    bool opponentActive(std::size_t index) const;
    // Police drive in every mode (MM2 never disables them per event).
    bool policeActive() const { return true; }

    RaceResult result() const;

    // Checkpoints of the current event ([0] is the start line).
    const std::vector<Checkpoint>& checkpoints() const { return m_checkpoints; }
    bool checkpointCleared(std::size_t i) const;
    // Whether a checkpoint's marker is shown (mmWaypointObject::Activate /
    // Deactivate as mmWaypoints drives them).
    bool checkpointVisible(std::size_t i) const;
    int targetCheckpoint() const { return m_wp.current; }
    // Checkpoint races: the player picks another target checkpoint
    // (mmWaypoints::GetNextWaypoint / GetLastWaypoint).
    void cycleTarget(bool forward);
    // mmHUD::SetMessage for rules run beside the session (mmMultiCR).
    void showMessage(std::string text, float seconds, bool top) { setMessage(std::move(text), seconds, top); }
    std::optional<Vec3> arrowTarget() const;
    // Progress shown on the HUD ("Check: n/N", "Lap: n/N", "Place: n/N").
    int checkpointsCleared() const;
    int checkpointsTotal() const;
    int lap() const; // current lap, 1-based (circuit)
    int laps() const { return m_setup.laps; }
    int position() const { return m_rank; }
    int racerCount() const { return static_cast<int>(m_opponents.size()) + 1; }

    float raceTime() const { return m_raceTime; }
    // Seconds left on the count-down clock (Blitz, timed lessons); < 0 = none.
    float timeRemaining() const;
    bool timeUp() const { return m_timeUp; }
    float lapTime() const;
    float lastLapTime() const { return m_lastLap; }
    float bestLapTime() const { return m_bestLap; }

    // Crash course.
    int lessonEvent() const { return m_lessonEvent; }
    const LessonEvent* currentLesson() const;
    // mmSingleStunt::UpdateEvade turns the overhead map on (mmViewMgr map
    // toggle) while its first countdown line shows, if it is off.
    bool wantsMap() const;
    int vehicleHits() const { return m_vehicleHits; }
    int objectHits() const { return m_objectHits; }

    // mmHUD::SetMessage (message) and SetMessage2 (message2, the line under
    // it; it shares the message's time and placement).
    const HudMessage& message() const { return m_message; }
    const HudMessage& message2() const { return m_message2; }
    std::vector<Event> takeEvents();
    MusicHint musicHint() const;

    // Where to put the player after Respawn.
    Mat34 respawnTransform() const { return m_respawn; }
    // The same as the modes' water handlers place the car (SetResetPos and
    // the reset angle, then mmPlayer::Reset): a checkpoint's place
    // (mmSingleCircuit / mmGameMulti::HitWaterHandler), or nothing for the
    // car's own reset place, its start (mmGame::HitWaterHandler, multiplayer
    // cruise). The start stays the car's reset place for a later restart.
    std::optional<ResetPlace> respawnPlace() const { return m_respawnPlace; }

private:
    // How the player's checkpoints work (mmWaypoints types).
    enum class WaypointRule : std::uint8_t {
        None,
        Circuit,       // 1: in order, waypoint 0 completes a lap
        CheckpointRace,// 2: any order, then the finish
        Blitz,         // 3: any order, done when all are cleared
        AnyOrderEnd,   // 4: crash course: any order, the last one must come last
        InOrder,       // 5: crash course: strictly in order
    };
    struct Waypoints {
        std::vector<char> cleared, visible;
        int current = 1;     // target
        int count = 1;       // waypoints passed, including the start
        int lastCleared = 0; // respawn point
        int lap = 0;
        bool finished = false;
        bool stopped = false; // no more hits (race over)
        bool singleVisible = false;
    };
    struct Racer {
        int count = 1;           // waypoints passed (the start counts)
        std::uint32_t mask = 1;  // checkpoint race: waypoints passed
        bool finished = false;
        int place = 0;
        float finishTime = 0.0f;
    };
    enum class Stage : std::uint8_t { Intro, Ready, Set };

    Session() = default;
    std::string str(std::uint32_t id, std::string_view fallback) const;
    void setMessage(std::string text, float seconds, bool top);
    void setMessage(std::uint32_t id, std::string_view fallback, float seconds, bool top);
    void setMessage2(std::string text);
    void push(EventType t, int index = -1, float value = 0.0f) { m_events.push_back({t, index, value}); }
    // AudSoundBase::PlayOnce / PlayLoop on the mode's sound (`mode` 0 once, 1 loop).
    void sound(GameSound s, float mode = 0.0f) { push(EventType::Sound, static_cast<int>(s), mode); }
    void speech(SpeechCue c, float value = 0.0f) { push(EventType::Speech, static_cast<int>(c), value); }
    void stopTimerWarning();
    bool lastEvent() const { return m_lessonEvent == static_cast<int>(m_setup.lessonEvents.size()) - 1; }

    void resetRace();
    void beginEvent(int index);
    void resetWaypoints();
    void updateCountdown(float dt);
    void enableLessonOpponents();
    void go();
    void updateRules(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                     std::span<const OpponentState> police);
    void tickMessage(float dt);
    void updateWaypoints(const PlayerState& player);
    void displayCleared(int index);
    void closestTarget(const Vec3& pos);
    void cycleCurrent(bool forward);
    void setTarget(int index);
    void updateOpponents(std::span<const OpponentState> opponents);
    void updateRank(const PlayerState& player, std::span<const OpponentState> opponents);
    bool updateHazards(float dt, const PlayerState& player);
    void hitWater();
    void dropThroughCity();
    void respawnAtLastCheckpoint();
    void updateClock(float dt);
    void timerWarning(float dt);
    void resetTimerWarning();
    void startPenalty(float seconds, bool held = true);
    void updateRace(float dt, const PlayerState& player);
    void updateLesson(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                      std::span<const OpponentState> police);
    bool copPursuit(const PlayerState& player, std::span<const OpponentState> police) const;
    // `latch`: the event sets the original's "race over" flag (+0x7c) before
    // moving on to the next event, so it stays set for the rest of the lesson.
    void lessonPassedOrNext(std::uint32_t passMessage, float seconds, bool top, float delay, bool latch);
    void lessonFailed(float delay = 5.0f, PlayerHold hold = PlayerHold::FinishBrake, bool registerFinish = true);
    void playerFinished();
    void endRace(bool finished, bool won, float delay, PlayerHold hold = PlayerHold::FinishBrake);
    int lessonOpponentOffset() const;
    WaypointRule rule() const;
    // mmPlayer::IsMaxDamaged, except in the frame a repair is on its way
    // (the original repairs the car in place; here the race screen does it
    // on DamageReset).
    bool wrecked(const PlayerState& p) const { return p.wrecked && !m_repairPending; }

    RaceSetup m_setup;
    const Strings* m_strings = nullptr;
    std::string m_city;
    SessionOptions m_options;

    std::vector<Checkpoint> m_checkpoints;
    Waypoints m_wp;
    std::vector<Racer> m_opponents;
    std::vector<char> m_oppEnabled;
    int m_finishers = 0;
    int m_rank = 1;

    Phase m_phase = Phase::Countdown;
    Stage m_stage = Stage::Intro;
    bool m_skipToGo = false; // later crash course events start at once
    bool m_preRaceCamera = false;
    bool m_startSignal = true;
    float m_wait = 0.0f;
    bool m_started = false;
    bool m_released = false;
    float m_raceTime = 0.0f; // mmHUD's race timer (+0xA54), stopped at the player's finish
    bool m_raceClock = false;
    // mmHUD's other timer (+0xA24), started and stopped with the race timer
    // by StartTimers / StopTimers but not at the player's finish: the
    // opponents' finish times.
    float m_hudTime = 0.0f;
    bool m_hudClock = false;
    float m_lapStart = 0.0f, m_lastLap = 0.0f, m_bestLap = 0.0f;
    std::vector<float> m_lapTimes;

    // Count-down clock (mmPlayer's timer at 0xd0c in the original).
    bool m_hasClock = false;
    bool m_clockRunning = false;
    float m_clock = 0.0f;
    bool m_timeUp = false;
    float m_warnAcc = 1.0f;
    bool m_warnBeeped = false, m_warnLoop = false;
    bool m_warnActive = false; // the warning sound was started (+0x76E8)

    // Penalties: a wrecked car held, then repaired.
    float m_penaltyLeft = 0.0f;
    bool m_penaltyHeld = false;
    bool m_repairPending = false;

    float m_waterTimer = 0.0f;
    bool m_waterHandled = false;
    float m_idleTime = 0.0f;
    float m_postWait = 0.0f;
    Mat34 m_respawn;
    std::optional<ResetPlace> m_respawnPlace;

    // Result.
    bool m_resultFinished = false;
    bool m_resultWon = false;
    PlayerHold m_endHold = PlayerHold::None; // set by endRace
    bool m_damagedOut = false, m_engineSilenced = false;
    int m_resultPosition = 0;
    float m_resultTime = 0.0f;
    float m_resultDamage = 0.0f;

    // Crash course.
    int m_lessonEvent = 0;
    int m_vehicleHits = 0, m_objectHits = 0;
    int m_baseVehicleImpacts = -1, m_baseObjectImpacts = -1;
    bool m_reachedMinSpeed = false;
    float m_belowMinSpeed = 0.0f;
    bool m_lessonDone = false; // the original's "race over" flag (+0x7c)
    float m_accelWait = -1.0f; // UpdateAccel's 2 s before the result

    HudMessage m_message, m_message2;
    std::vector<Event> m_events;
};

} // namespace mm2::game::session
