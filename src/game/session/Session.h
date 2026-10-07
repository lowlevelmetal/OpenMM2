#pragma once

// Race rules for every single-player mode, ported from MM1's mmGame,
// mmSingleBlitz, mmSingleCircuit, mmGameSingle and mmWaypoints (Open1560
// game.asm) and extended with MM2's Checkpoint race and Crash Course.
//
// Open1560 - An Open Source Re-Implementation of Midtown Madness 1 Beta
// Copyright (C) 2020 Brick. GPL-3.0-or-later; OpenMM2 port under the same licence.
//
// Typical use (RaceScreen):
//
//   auto s = Session::create(config, city, vfs, strings, &error);
//   player.reset(s->playerSpawn());           // and opponents at s->opponents()[i].spawn
//   s->start();
//   every frame:
//     s->update(dt, playerState, opponentStates);
//     if (s->playerHeld()) brake fully, ignore throttle;
//     if (!s->racersReleased()) hold the AI cars;
//     for (auto e : s->takeEvents()) ... (audio, Respawn -> place the car at s->respawnTransform())
//     hud.draw(...); markers.draw(...);
//     if (s->finished()) show results with s->result();

#include "city/CityData.h"
#include "game/RaceConfig.h"
#include "game/Strings.h"
#include "game/session/Gate.h"
#include "game/session/RaceSetup.h"
#include "game/session/Types.h"
#include "vfs/Vfs.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mm2::game::session {

enum class Phase : std::uint8_t {
    Countdown, // Ready / Set (racers held)
    Racing,
    PostRace,  // finished, timed out or wrecked; results follow after a delay
    Done,      // show the results
};

struct SessionOptions {
    float scoringBias = 1.0f;  // tune/<car>.info ScoringBias (race score multiplier)
    CarExtent playerExtent;    // half width / half length of the player's car
    bool skipCountdown = false;
};

class Session {
public:
    static std::unique_ptr<Session> create(const RaceConfig& config, const city::CityData& city,
                                           const vfs::Vfs& vfs, const Strings& strings,
                                           std::string* error = nullptr, const SessionOptions& options = {});

    const RaceSetup& setup() const { return m_setup; }
    GameMode mode() const { return m_setup.config.mode; }

    // Starting places.
    Mat34 playerSpawn() const { return m_setup.playerSpawn; }
    const std::vector<OpponentSetup>& opponents() const { return m_setup.opponents; }
    const std::vector<PoliceSetup>& police() const { return m_setup.police; }

    void start();
    // `opponents` in the order of opponents(); `police` in the order of
    // police() (crash course chasers, cop chase lessons).
    void update(float dt, const PlayerState& player, std::span<const OpponentState> opponents = {},
                std::span<const OpponentState> police = {});

    Phase phase() const { return m_phase; }
    bool finished() const { return m_phase == Phase::Done; }
    // The player's car must stay braked (countdown, false-start penalty).
    bool playerHeld() const;
    // AI racers may drive.
    bool racersReleased() const { return m_released; }
    // Opponents active in the current crash course event (the car to follow
    // or to ram; chasers). Race modes: all.
    bool opponentActive(std::size_t index) const;
    bool policeActive() const;

    RaceResult result() const;

    // Checkpoints of the current event ([0] is the start line).
    const std::vector<Checkpoint>& checkpoints() const { return m_checkpoints; }
    bool checkpointCleared(std::size_t i) const;
    // Whether a checkpoint's marker should be shown (not cleared; the finish
    // only once it is open).
    bool checkpointVisible(std::size_t i) const;
    int targetCheckpoint() const { return m_player.target; }
    std::optional<Vec3> arrowTarget() const;
    // Progress shown on the HUD ("Check: n/N", "Lap: n/N", "Place: n/N").
    int checkpointsCleared() const;
    int checkpointsTotal() const;
    int lap() const;   // current lap, 1-based (circuit)
    int laps() const { return m_setup.laps; }
    int position() const { return m_player.rank; }
    int racerCount() const { return static_cast<int>(m_opponents.size()) + 1; }

    float raceTime() const { return m_raceTime; }
    // Seconds left on a count-down clock (Blitz, timed lessons); < 0 = none.
    float timeRemaining() const;
    float lapTime() const;
    float lastLapTime() const { return m_player.lastLap; }
    float bestLapTime() const { return m_player.bestLap; }

    // Crash course.
    int lessonEvent() const { return m_lessonEvent; }
    const LessonEvent* currentLesson() const;
    int vehicleHits() const { return m_vehicleHits; }
    int objectHits() const { return m_objectHits; }

    const HudMessage& message() const { return m_message; }
    const HudMessage& message2() const { return m_message2; }
    std::vector<Event> takeEvents();
    MusicHint musicHint() const;

    // Where to put the player after HitWater / falling out of the city.
    Mat34 respawnTransform() const { return m_respawn; }

private:
    struct Racer {
        bool player = false;
        bool active = true;
        Vec3 prevPos;
        bool havePrev = false;
        std::vector<char> cleared; // any-order modes
        int clearedCount = 0;
        int target = -1;           // next/nearest checkpoint
        int lap = 0;               // completed laps (circuit)
        int passedThisLap = 0;
        bool finished = false;
        bool dnf = false;
        float finishTime = 0.0f;
        int finishPosition = 0;
        float lapStart = 0.0f;
        float lastLap = 0.0f;
        float bestLap = 0.0f;
        int rank = 1;
        float distanceToTarget = 0.0f;
    };

    Session() = default;
    std::string str(std::uint32_t id, std::string_view fallback) const;
    void setMessage(std::string text, float seconds, bool top = false);
    void setMessage(std::uint32_t id, std::string_view fallback, float seconds, bool top = false);
    void push(EventType t, int index = -1, float value = 0.0f) { m_events.push_back({t, index, value}); }

    void beginEvent(int index);
    void updateCountdown(float dt, const PlayerState& player);
    void updateRacer(Racer& r, const Mat34& car, bool player);
    void clearAnyOrder(Racer& r, int index, bool player);
    void advanceInOrder(Racer& r, bool player);
    void finishRacer(Racer& r, bool player);
    void updateRanks();
    void updateTarget(Racer& r, const Vec3& pos);
    void checkHazards(float dt, const PlayerState& player);
    void updateLesson(float dt, const PlayerState& player, std::span<const OpponentState> opponents);
    void failLesson(std::uint32_t id, std::string_view fallback);
    void endRace(bool finished, bool won, std::uint32_t messageId, std::string_view fallback);
    bool anyOrder() const;
    bool hasOpponentRace() const;

    RaceSetup m_setup;
    const Strings* m_strings = nullptr;
    std::string m_city;
    float m_dropY = -50.0f;
    SessionOptions m_options;

    std::vector<Checkpoint> m_checkpoints;
    Racer m_player;
    std::vector<Racer> m_opponents;

    Phase m_phase = Phase::Countdown;
    int m_countdownStep = 0; // 0 Ready, 1 Set
    float m_stateWait = 0.0f;
    bool m_started = false;
    bool m_released = false;
    bool m_penalty = false;
    float m_penaltyLeft = 0.0f;
    float m_raceTime = 0.0f;
    float m_clock = 0.0f; // count-down clock
    bool m_hasClock = false;
    int m_lastWarnSecond = -1;
    float m_idleTime = 0.0f;
    float m_playerSpeedMph = 0.0f;
    float m_waterTimer = 0.0f;
    int m_finishOrder = 0;
    Mat34 m_respawn;

    // Result.
    bool m_resultFinished = false;
    bool m_resultWon = false;
    int m_resultPosition = 0;
    float m_resultTime = 0.0f;
    float m_resultDamage = 0.0f;

    // Crash course.
    int m_lessonEvent = 0;
    int m_vehicleHits = 0, m_objectHits = 0;
    int m_baseVehicleImpacts = -1, m_baseObjectImpacts = -1;
    bool m_reachedMinSpeed = false;
    bool m_lessonFailed = false;

    HudMessage m_message, m_message2;
    std::vector<Event> m_events;
};

} // namespace mm2::game::session
