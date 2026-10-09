#pragma once

// The start of a network race on one machine, from the moment its race has
// loaded until its cars go: MM2's mmMultiRace / mmMultiCircuit /
// mmMultiBlitz::UpdateGame state 0 (and mmMultiRoam::Init, mmMultiCR::Init)
// on OpenMM2's shared start time (net::Session; docs/multiplayer.md, "Race
// start").
//
// MM2's race modes sit in state 0 after loading with every car held
// (InitNetworkPlayers: vehCar::SetDrivable(0, 1)) and nothing shown. After 5 s
// of frames the machine sends RaceReady (0x1f6, SendRaceReady) to everyone;
// from then on, while some other player's RaceReady has not arrived, it shows
// "Waiting for N players" (strings 31-37). The host sends the start (0x20f)
// once every player has; each machine then counts Ready... (1.25 s), Set...
// (1.25 s) and Go!, when the cars go and the race timers start. OpenMM2's
// host sends the session time of the Go instead, so every countdown ends
// together, and stops waiting for a player still loading after
// net::SessionConfig::loadWaitMs: that machine, loaded after the start,
// counts down on its own and joins the running race, as an MM2 machine does
// with a start message that waited in its queue.

#include "game/RaceConfig.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mm2::game {

class NetGame;
class Strings;

class NetRaceStart {
public:
    enum class Kind {
        // Blitz, circuit and checkpoint races (mmMultiBlitz / mmMultiCircuit /
        // mmMultiRace): RaceReady after 5 s, "Waiting for N players", then
        // Ready... / Set... / Go! up to the start.
        Race,
        // Cops and Robbers: mmMultiCR::Init reports at once (0x25c) and the
        // game has no countdown ("Go!"). MM2's host starts without waiting
        // and each joiner when the host's game state reaches it; OpenMM2 waits
        // for everyone as in the races, since its rules and limits run on
        // the shared start's clock (deviation).
        CopsAndRobbers,
        // Cruise: mmMultiRoam::Init reports at once (PlayerFinishedLoading,
        // SendGameSet 0x1fa) and lets the car go: nobody waits.
        Cruise,
    };
    static Kind kindOf(GameMode mode);
    // The seconds of frames state 0 counts before RaceReady (+0x408 against
    // 5.0).
    static constexpr float kSettleSeconds = 5.0f;
    // From the start message to "Go!": states 1 and 2, 1.25 s each. Cops and
    // Robbers and cruise have none.
    static float countdownSeconds(Kind kind);

    struct Input {
        bool startKnown = false;     // the host's start has arrived
        double secondsToStart = 0.0; // until the shared start (the Go), when known
        int othersLoading = 0;       // other players in the session who have not reported
    };
    struct Output {
        bool report = false;              // report the race loaded now
        std::optional<float> secondsToGo; // for Session::setNetStart: none until the start
        int waitingFor = 0;               // > 0: "Waiting for N players"
        bool held = true;                 // the car may not move yet
        bool countdownBegan = false;      // this frame: Ready... (for the log)
        bool went = false;                // this frame: Go!
        std::vector<std::uint8_t> joined; // cruise: other players whose report has just come
    };

    explicit NetRaceStart(Kind kind) : m_kind(kind) {}

    // One frame of `dt` seconds.
    Output update(float dt, const Input& in);
    // The same with the network's state; reports the race loaded through it
    // and logs the countdown's start and the Go in session time.
    Output update(float dt, NetGame& net);

    Kind kind() const { return m_kind; }
    bool reported() const { return m_reported; }
    // Loaded after the start: counting down on its own.
    bool ownCountdown() const { return m_own; }
    bool gone() const { return m_gone; }

    // "Waiting for 1 player" ... "Waiting for 7 players": strings 31-37
    // (mmGameMulti::InitGameStrings joins them with '|', UpdateGame shows the
    // N-th with string::SubString). OpenMM2's sessions can hold more.
    static std::string waitingText(const Strings& strings, int players);

private:
    Kind m_kind;
    float m_settle = 0.0f;
    bool m_reported = false;
    bool m_startSeen = false;
    bool m_own = false;
    float m_ownLeft = 0.0f;
    bool m_began = false;
    bool m_gone = false;
    std::set<std::uint8_t> m_joined;
};

} // namespace mm2::game
