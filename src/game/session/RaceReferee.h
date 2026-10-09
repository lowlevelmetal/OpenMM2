#pragma once

// The host's referee of a network race (OpenMM2: the host is the authority
// for the rules, docs/multiplayer.md "Rules"). MM2's machines each ran
// mmWaypoints on their own car and told the others (mmGameMulti::
// SendPosition's waypoint count, SendFinishReq); the host acknowledged the
// finishes, ran the finish timeout and ended the race (mmMultiRace /
// mmMultiCircuit / mmMultiBlitz::UpdateGame and GameMessage 0x206, 0x1fe,
// 0x211). OpenMM2's host simulates every player's car, so it runs those
// waypoints itself, with the same rules (WaypointTracker), on every car
// after each physics sample, and decides:
//
// * each car's checkpoints, laps and finish, and its finish time: the
//   samples the host simulated it from the one its own input released it
//   (its player's Go at the shared start) to the one it crossed the line in,
//   1/60 s each;
// * a Blitz car out of time (its own clock), and the host's own time running
//   out ending everyone's race (0x1fe);
// * the finish timeout from the first finish (60 s in a checkpoint race,
//   120 s in a circuit; SetTimeoutOn) and every player still racing when it
//   runs out not finishing (0x1fe), and the end once every player still in
//   the race is counted (0x211);
// * the standings: each player's place and each car's icon number as every
//   player's own machine would rank them (mmGameMulti::UpdateScore).

#include "game/RaceConfig.h"
#include "game/session/Types.h"
#include "game/session/Waypoints.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace mm2::game::session {

class RaceReferee {
public:
    // A time that means "did not finish" (mmGameMulti's 86400).
    static constexpr float kDnf = 86400.0f;

    struct Config {
        GameMode mode = GameMode::Checkpoint; // Blitz, Circuit or Checkpoint
        std::vector<Checkpoint> checkpoints;
        int laps = 1;
        float timeLimit = 0.0f; // Blitz: seconds on each car's clock (0: none)
        std::uint8_t host = 0;  // the host's player id (its time running out ends a Blitz)
        float sampleSeconds = 1.0f / 60.0f;
    };

    // What the referee decided, in order.
    struct Decision {
        enum class Kind : std::uint8_t {
            Finished,   // `player` finished in `seconds` (kDnf: did not finish)
            TimedOut,   // 0x1fe: the race is over for everyone still racing
            AllCounted, // 0x211: every player still in the race is counted
        };
        Kind kind{};
        std::uint8_t player = 0;
        float seconds = 0.0f;
    };

    struct Player {
        WaypointTracker wp;
        std::vector<std::uint8_t> hits; // the waypoints its car hit, in order
        bool inRace = true;  // in the session and not quit: waited for
        bool placed = false; // its car is in the world (the host simulates it)
        bool released = false;
        std::uint32_t releaseSeq = 0; // its first sample not held
        std::uint32_t evaluated = 0;  // the last sample the rules saw
        Vec3 position;
        std::optional<float> finish; // decided: seconds, or kDnf
        // mmGameMulti::UpdateScore on its machine: its place among the
        // racers (frozen once its waypoints are done) and how many race.
        int place = 1, racers = 1;
    };

    explicit RaceReferee(Config config);

    const Config& config() const { return m_config; }

    // A player in the race (waited for before it has a car).
    void addPlayer(std::uint8_t id);
    // A player left the session or quit the race: no longer waited for (a
    // finish it had stays in the results).
    void removePlayer(std::uint8_t id);
    // After each physics sample of a player's car: `seq` the number of the
    // input the sample applied, `held` whether that input held the car
    // (vehCar::SetDrivable(0, 1): the grid, a wreck penalty, the finish).
    void sample(std::uint8_t id, std::uint32_t seq, const Mat34& car, const Vec3& inertiaBox, bool held);
    // Once a frame (`dt` seconds of the host's clock): the finish timeout,
    // the end, the standings.
    void update(float dt);

    std::vector<Decision> takeDecisions();
    const Player* player(std::uint8_t id) const;
    const std::map<std::uint8_t, Player>& players() const { return m_players; }
    // mmGameMulti::SortResults' table: by time, a later equal time after.
    struct Result {
        std::uint8_t player = 0;
        float seconds = 0.0f;
    };
    const std::vector<Result>& results() const { return m_results; }
    bool timedOut() const { return m_timedOut; }
    bool allCounted() const { return m_allCounted; }
    // A car's icon number on `viewer`'s machine (mmGameMulti::UpdateScore's
    // IconIndex): 0 no icon (finished, or no car), else its place.
    int iconPlace(std::uint8_t viewer, std::uint8_t car) const;

private:
    void decideFinish(std::uint8_t id, Player& p, float seconds);
    void timeOutAll();
    bool waitsForAll() const {
        return m_config.mode == GameMode::Checkpoint || m_config.mode == GameMode::Circuit;
    }
    float timeout() const { return m_config.mode == GameMode::Circuit ? 120.0f : 60.0f; }
    Vec3 targetOf(const Player& p) const;

    Config m_config;
    std::map<std::uint8_t, Player> m_players;
    std::vector<Result> m_results;
    std::vector<Decision> m_decisions;
    bool m_timeoutOn = false;
    float m_timeout = 0.0f;
    bool m_timedOut = false;
    bool m_allCounted = false;
};

} // namespace mm2::game::session
