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
#include "game/session/Waypoints.h"
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
    // Multiplayer cruise and Cops and Robbers: the seed mmGame::RespawnXYZ
    // draws the player's start from (MM2: the local player's DirectPlay id;
    // OpenMM2: 1 + its network id).
    std::uint32_t seed = 1;
    // Line of sight between two points for mmSingleStunt::CheckCopPursuit
    // (the original probes the level); unset = always clear.
    std::function<bool(const Vec3& from, const Vec3& to)> lineOfSight;
    // The local player's network name: multiplayer races show it over
    // "finished in" (mmMultiBlitz / mmMultiCircuit / mmMultiRace::UpdateGame).
    std::string playerName;
    // Multiplayer: this machine hosts the session (the modes' host and client
    // lines differ: e.g. "finished in" 152 / 150).
    bool netHost = false;
    // Multiplayer races under the host's authority (OpenMM2,
    // docs/multiplayer.md "Rules"): the session predicts the player's
    // checkpoints from its car and takes the host's word on them
    // (applyNetProgress); the finish, the standings, the finish timeout and
    // the end are the host's (netFinished, setNetStanding, netTimedOut,
    // netAllCounted). Without it every machine decides its own (MM2's
    // peers: remoteFinished and NetFinished).
    bool netRules = false;
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
    // Cruise and Cops and Robbers (setup().respawnStart): the mode's
    // InitOtherPlayers, which mmGame::Init runs after aiMap::Reset, moves
    // the player's start to mmGame::RespawnXYZ's pick (cruiseStart; the
    // multiplayer seed is SessionOptions::seed). `globalSeed` is MM2's
    // random seed as aiMap::Reset left it (ai::World::resetAndPopulate);
    // `findRoom` is the level's room lookup. Returns the pick (nothing when
    // the mode has no such start); playerSpawn(), setup().playerPlace and the
    // water respawn follow it.
    std::optional<RespawnPick> placeRespawnStart(const city::CityData& city, std::uint32_t globalSeed,
                                                 const RoomLookup& findRoom);
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
    // waits for it in state 0 and then runs on its own clock; Cops and
    // Robbers says "Go!" with it). Single player ignores it.
    void setStartSignal(bool received) {
        m_startSignal = received || !multiplayer();
        m_netToGo.reset();
    }
    // The same on OpenMM2's shared start (game::NetRaceStart): none while
    // the start has not come, then the seconds left until the start, which
    // the countdown follows (Ready... while more than 1.25 s are left, Set...
    // while any are, Go! at 0) so that every machine ends it together.
    void setNetStart(std::optional<float> secondsToGo) {
        m_startSignal = secondsToGo.has_value() || !multiplayer();
        m_netToGo = secondsToGo;
    }
    // `opponents` in the order of opponents(); `police` in the order of
    // police() (crash course chasers, cop chase lessons).
    //
    // Expects the cars' physics matrices (vehCarSim's phInertialCS, i.e. at
    // the centre of gravity): every rule of the original tests those.
    void update(float dt, const PlayerState& player, std::span<const OpponentState> opponents = {},
                std::span<const OpponentState> police = {});
    // A frame with the game paused (asRoot's pause: the menu, the full-screen
    // map): the rules, the clocks and the AI stand still, but mmGame::Update
    // still runs its fall and water checks and mmHUD::Update still counts
    // the message down.
    void updatePaused(float dt, const PlayerState& player);
    // The whole race starts over (mmGame::Reset, e.g. the pause menu's
    // Restart); emits Restart.
    void restart();

    Phase phase() const { return m_phase; }
    bool finished() const { return m_phase == Phase::Done; }
    // mmGame's race-over flag (+0x7c): set by a finish (the race modes) or by
    // most lesson endings, cleared by mmGame::Reset. With the main menu
    // locked after the race, Escape shows the results only when it is set
    // (mmPopup::Update).
    bool raceOver() const { return mode() == GameMode::CrashCourse ? m_lessonDone : m_resultFinished; }
    // What the game does to the player's car: undrivable before "Go!", during
    // wreck penalties and after some endings (a wreck, any multiplayer
    // finish), braked by mmPlayer +0x2258 after the others.
    PlayerHold playerHold() const;
    bool playerHeld() const { return playerHold() == PlayerHold::Undrivable; }
    // A single-player race or Blitz lost to a wreck ("damaged out"): the
    // music stops at once and the engine falls silent
    // (vehCarAudioContainer::SilenceEngine), as after the water in a race.
    bool damagedOut() const { return m_damagedOut; }
    // Whether the ending turned to the post-race camera (mmPlayer::
    // SetPostRaceCam / mmGameMulti::SetFinishCam).
    bool postRaceCamera() const { return m_postRaceCam; }
    // Whether the ending stopped the music (the race modes' StopSegment(0) at
    // the finish; a wreck's StopSegment(1) is damagedOut()). The crash course
    // and the multiplayer modes never stop it.
    bool musicStopped() const { return m_musicStop; }
    bool engineSilenced() const { return m_engineSilenced; }
    // AI racers may drive (mmGameSingle::EnableRacers at "Go!").
    bool racersReleased() const { return m_released; }
    // The player's vehCarDamage EnableDamage (+0x2D): DisableRacers (the race
    // modes' and the crash course's Reset) turns it off until EnableRacers at
    // "Go!", which a Jump lesson never calls; the circuit finish turns it off
    // again. Cruise and Cops and Robbers keep it on.
    bool playerDamageEnabled() const { return m_playerDamage; }
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
    // mmHUD::SetMessage2: a line under the current message.
    void showMessage2(std::string text) { setMessage2(std::move(text)); }
    std::optional<Vec3> arrowTarget() const;
    // Progress shown on the HUD ("Check: n/N", "Lap: n/N", "Place: n/N").
    int checkpointsCleared() const;
    int checkpointsTotal() const;
    int lap() const; // current lap, 1-based (circuit)
    int laps() const { return m_setup.laps; }
    int position() const { return m_rank; }
    // Checkpoint races and circuits: an opponent's place as its icon shows
    // it (mmSingleRace / mmSingleCircuit::UpdateScore write it into the
    // opponent's OppIconInfo); 10 (no number) before the first update and in
    // the other modes.
    int opponentPlace(std::size_t index) const {
        return index < m_opponentPlaces.size() ? m_opponentPlaces[index] : 10;
    }
    // Multiplayer races: another network player's rank over its icon
    // (mmGameMulti::UpdateScore), in setNetRacers() order; 10 = no number,
    // 0 = no icon (finished, or no car).
    int netRacerPlace(std::size_t i) const { return i < m_netPlaces.size() ? m_netPlaces[i] : 10; }
    int racerCount() const { return multiplayer() ? m_netRacerCount : static_cast<int>(m_opponents.size()) + 1; }

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

    // --- Multiplayer races (mmGameMulti, mmMultiRace / Circuit / Blitz) ---
    // A time that means "did not finish" (mmGameMulti::UpdateResults'
    // AddLoser): 24 hours.
    static constexpr float kNetDnf = 86400.0f;
    // The other players as mmGameMulti::UpdateScore sees them, every frame.
    struct NetRacer {
        std::string name;
        int waypoints = 1; // waypoints passed (mmPlayer +0x2254, the start included)
        Vec3 position;     // the car's (inertial) position
        bool present = true; // its car is in the race (mmNetObject +0x118 / +0x11c)
        bool finished = false;
        // SessionOptions::netRules: its icon's number as the host ranks it
        // on this machine (0 no icon).
        int hostPlace = 0;
    };
    void setNetRacers(std::vector<NetRacer> racers) { m_netRacers = std::move(racers); }
    // Another player's finish (or kNetDnf) arrived (mmMulti*::GameMessage
    // 0x206 / 0x1f7): Messagenote, "<name>" / "finished in M:SS:HH", the
    // results list, and the finish timeout of a race or circuit.
    void remoteFinished(const std::string& name, float seconds);
    // Waypoints passed, the start included (sent to the other players).
    int waypointsPassed() const { return m_wp.count; }

    // --- Network races under the host's authority (SessionOptions::netRules) ---
    // A predicted checkpoint the host has not counted by this many of the
    // car's samples after the one it was hit in is taken back.
    static constexpr std::uint32_t kNetHitMargin = 30;
    // This machine's newest physics sample (its car's input number): the
    // checkpoints the session predicts are stamped with it.
    void setNetSample(std::uint32_t seq) { m_netSample = seq; }
    // The host's word on the player's waypoints: the waypoints its car hit,
    // in order, as the host's simulation of it saw them up to its sample
    // `evaluated` (`first`: the number of the first of `hits`, the earlier
    // ones as this machine has them). The predicted hits the host has
    // counted are confirmed; the ones it has not counted within
    // kNetHitMargin samples are taken back (silently: the marker shows
    // again); the ones it counted that were not predicted are shown now.
    struct NetProgress {
        std::uint32_t evaluated = 0;
        std::uint32_t first = 0;
        std::vector<std::uint8_t> hits;
    };
    void applyNetProgress(const NetProgress& progress, const Vec3& carPosition);
    // The waypoints this machine's car has hit, in order (for tests and
    // the trace).
    std::vector<std::uint8_t> netHits() const;
    // The host's standings for the player (mmGameMulti::UpdateScore on this
    // machine: "Place: n/N").
    void setNetStanding(int place, int racers);
    // The host's finish for the player (mmGameMulti::SendFinishAck to it:
    // its line and its results), or kNetDnf.
    void netFinished(float seconds);
    // 0x1fe from the host: the race is over for everyone still racing
    // ("Race over"; a Blitz with the net alert).
    void netTimedOut();
    // 0x211 from the host: every player is counted, the results follow.
    void netAllCounted() { m_netAllCounted = true; }
    bool netFinishKnown() const { return m_netFinishKnown; }

    // Where to put the player after Respawn.
    Mat34 respawnTransform() const { return m_respawn; }

    // A network race (netRules): mmGame::Update's water and fall checks run
    // on the car's samples (game::NetCarDriver, on the host for every car,
    // here predicted for this machine's), not here; the session shows only
    // the water's message (and HitWater). Where the water's handler puts
    // the car: the last checkpoint it cleared in a race
    // (mmGameMulti::HitWaterHandler), nothing elsewhere (back to its reset
    // position: mmGame::HitWaterHandler, mmMultiCR's).
    std::optional<Mat34> netRespawnPoint() const;
    // The handler put this machine's car back: the water's message may
    // show again.
    void netWaterReset();

private:
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
    void deactivateFinish();
    bool lastEvent() const { return m_lessonEvent == static_cast<int>(m_setup.lessonEvents.size()) - 1; }

    void resetRace();
    void beginEvent(int index);
    void updateCountdown(float dt);
    void enableLessonOpponents();
    void go();
    void copsAndRobbersGo();
    void updateRules(float dt, const PlayerState& player, std::span<const OpponentState> opponents,
                     std::span<const OpponentState> police);
    void tickMessage(float dt);
    void updateWaypoints(const PlayerState& player);
    // What the waypoints' rules did, on the HUD (mmWaypoints::Update's
    // DisplayHUDMessage, mmHUD::ShowSplitTime and PostLapTime, the sounds).
    void showWaypointSteps(const std::vector<WaypointStep>& steps);
    void displayCleared(int index, int count);
    void lapCompleted(int lap);
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
    WaypointTracker m_wp;
    std::vector<Racer> m_opponents;
    std::vector<char> m_oppEnabled;
    int m_finishers = 0;
    int m_rank = 1;
    std::vector<int> m_opponentPlaces; // OppIconInfo places (hud-views)

    Phase m_phase = Phase::Countdown;
    Stage m_stage = Stage::Intro;
    bool m_skipToGo = false; // later crash course events start at once
    bool m_preRaceCamera = false;
    bool m_startSignal = true;
    std::optional<float> m_netToGo; // setNetStart
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

    // Result.
    bool m_resultFinished = false;
    bool m_resultWon = false;
    PlayerHold m_endHold = PlayerHold::None; // set by endRace
    bool m_damagedOut = false, m_engineSilenced = false;
    bool m_postRaceCam = false, m_musicStop = false;
    bool m_playerDamage = true;

    // Multiplayer races.
    struct NetResult {
        std::string name;
        float time = 0.0f;
        bool self = false;
    };
    std::vector<NetRacer> m_netRacers;
    std::vector<int> m_netPlaces; // the other players' icons' IconIndex
    std::vector<NetResult> m_netResults; // mmGameMulti::SortResults' table, by time
    int m_netRacerCount = 1;
    bool m_netTimeoutOn = false;         // SetTimeoutOn / SetTimeoutOff
    float m_netTimeout = 0.0f;
    bool m_netTimedOut = false;
    bool netWaitsForAll() const;
    void addNetResult(std::string name, float time, bool self);
    void updateNetRace(float dt, const PlayerState& player);
    int m_resultPosition = 0;
    float m_resultTime = 0.0f;
    float m_resultDamage = 0.0f;
    // SessionOptions::netRules.
    struct NetHit {
        std::uint8_t index = 0;
        std::uint32_t seq = 0; // this machine's sample it was hit in (or the host's word came)
    };
    std::vector<NetHit> m_netHits; // every hit m_wp took, in order
    std::uint32_t m_netSample = 0;
    std::uint32_t m_netEvaluated = 0;
    bool m_netFinishKnown = false;
    bool m_netAllCounted = false;
    bool m_netWord = false; // showing what the host's word brought
    bool netRules() const { return m_options.netRules && multiplayer(); }

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
