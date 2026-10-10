#pragma once

// The rules of a network race decided by the host (OpenMM2: the host is the
// authority, docs/multiplayer.md "Rules"; the wire is net/RulesState.h).
//
//   host    runs the race's referee (session::RaceReferee: every player's
//           checkpoints, laps, finish and time from its simulated cars,
//           the finish timeout, the end, the standings) or takes Cops and
//           Robbers' decisions (session::CopsAndRobbers::updateHost), shows
//           its own player's as a client would, and sends every other
//           player, once a second and at once when something happened,
//           what it decided since its last message and the state as it
//           stands (reliably); in a race the state alone also goes
//           unreliably 20 times a second and at once on a new hit
//           (protocol 14: a lost packet no longer holds the confirmation
//           of a checkpoint back until it is sent again).
//   client  takes the host's messages only: the decisions are shown once
//           each (in order, by their numbers), the newest state (of
//           either kind) corrects what the client predicted (the
//           checkpoints its car hit, a gold pickup) and stands in for
//           anything it missed.
//
// Nothing here touches the network: the race screen hands the messages to
// NetGame and feeds the received game events in.

#include "game/net/NetGame.h"
#include "game/session/CopsAndRobbers.h"
#include "game/session/RaceReferee.h"
#include "game/session/Session.h"
#include "net/RulesState.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mm2::game {

// The host's event filter (net::Session::setEventFilter): a player's own
// word on a rule (a checkpoint, a lap, a finish, the gold, Cops and
// Robbers' places and limits, the host's rules message) is refused; only
// the host decides them.
bool hostAcceptsGameEvent(std::uint8_t from, std::uint16_t type);

class NetRules {
public:
    // How often a player hears from the host reliably when nothing happened
    // (in Cops and Robbers, whose state goes no other way, four times a
    // second), and in a race unreliably (the standings, the samples its
    // rules have seen; the CarStates rate).
    static constexpr std::uint64_t kStateIntervalMs = 1000;
    static constexpr std::uint64_t kCopsStateIntervalMs = 250;
    static constexpr std::uint64_t kFastStateIntervalMs = 50;
    // The waypoints a message lists at most (the newest; a client keeps the
    // earlier ones as it has them), and an unreliable state.
    static constexpr std::size_t kHitWindow = 64;
    static constexpr std::size_t kStateHitWindow = 16;

    struct Setup {
        bool host = false;
        std::uint8_t self = 0;
        std::uint32_t race = 0;                    // NetGame::raceNumber()
        session::Session* session = nullptr;       // a race's rules (Blitz, circuit, checkpoint race)
        session::CopsAndRobbers* cops = nullptr;   // Cops and Robbers'
        std::vector<std::uint8_t> players;         // the players in the race at its start
        float sampleSeconds = 1.0f / 60.0f;
    };
    void begin(const Setup& setup);
    bool active() const { return m_setup.session != nullptr || m_setup.cops != nullptr; }
    bool host() const { return m_setup.host; }
    const std::vector<std::uint8_t>& players() const { return m_setup.players; }
    // A player's name for the lines ("<name> finished in"); kept after it leaves.
    void setName(std::uint8_t id, std::string name) { m_names[id] = std::move(name); }
    // OPENMM2_NET_TRACE's rules lines (game/net/RulesTrace.h), this frame.
    void setTrace(std::FILE* f, double frameTime) {
        m_trace = f;
        m_frameTime = frameTime;
    }

    // --- Host ---------------------------------------------------------------------------
    // After each physics sample of a player's car (the host's own too): `seq`
    // the input's number, `held` whether it held the car.
    void hostSample(std::uint8_t id, std::uint32_t seq, const Mat34& car, const Vec3& inertiaBox, bool held);
    // The checkpoint the water's handler puts a player's car back at in a
    // race (the last one the referee counted it clearing,
    // mmGameMulti::HitWaterHandler), or nothing outside a race's host.
    std::optional<int> respawnIndex(std::uint8_t id) const {
        return m_referee ? m_referee->lastCleared(id) : std::nullopt;
    }
    // A player left the session or quit the race.
    void playerLeft(std::uint8_t id);
    // Cops and Robbers: what the host's rules decided this frame.
    void hostDecided(const std::vector<session::CopsAndRobbers::Message>& decisions);
    // Once a frame, after the rules: the referee's frame, its word on the
    // host's own player (`ownPosition` its car), and each player's message
    // when it is due (`send(player, payload)` as a game event of type
    // net::kRulesEvent to that player).
    // `sendState(player, state)`: a race's state alone, unreliably
    // (NetGame::sendRulesState).
    using Send = std::function<void(std::uint8_t player, std::vector<std::byte> payload)>;
    using SendState = std::function<void(std::uint8_t player, const net::RulesStateMsg& state)>;
    void hostUpdate(float dt, std::uint64_t nowMs, const Vec3& ownPosition, const Send& send,
                    const SendState& sendState = {});

    // --- Client -------------------------------------------------------------------------
    // The host's messages among the frame's game events (`carPosition`: this
    // machine's car, where a corrected target is measured from).
    void receive(const std::vector<NetGameEvent>& events, const Vec3& carPosition);
    // The host's unreliable states (NetGame::takeRulesStates), the newest
    // taken.
    void receiveStates(const std::vector<net::RulesMsg>& states, const Vec3& carPosition);
    // This machine predicted its car took the gold at its sample `seq`.
    void predictedPickup(std::uint32_t seq) { m_pickupSample = seq; }

    // --- Both ---------------------------------------------------------------------------
    // A car's icon number on this machine as the host ranks it (0: none).
    int iconPlace(std::uint8_t id) const;
    // Whether the host has decided that player's finish (or its did-not-finish).
    bool finished(std::uint8_t id) const { return m_finished.contains(id); }
    const session::RaceReferee* referee() const { return m_referee.get(); }
    // The last sample of this machine's car the host's rules have seen.
    std::uint32_t evaluated() const { return m_evaluated; }

    struct Stats {
        std::uint64_t sent = 0, bytes = 0;     // host: messages and their payload bytes
        std::uint64_t states = 0, stateBytes = 0; // host: unreliable states and their bytes
        std::uint64_t received = 0;            // client: messages taken
        std::uint64_t statesTaken = 0;         // client: unreliable states taken (newer than any)
        std::uint64_t refused = 0;             // client: rules messages not from the host
        std::uint64_t malformed = 0, stale = 0; // client: undecodable; another race's or older
        std::uint64_t decisions = 0, gaps = 0; // decisions applied; numbers missing (the state stood in)
    };
    const Stats& stats() const { return m_stats; }
    // Every decision so far (host), or every one applied (client), in order.
    const std::vector<net::RuleDecisionMsg>& decisions() const { return m_log; }

private:
    struct Client {
        std::uint32_t seq = 0;          // messages and states sent (one numbering)
        std::uint32_t lastDecision = 0; // the newest decision sent
        std::size_t hits = 0;           // its car's hits when last sent
        std::size_t stateHits = 0;      // ... when last sent unreliably
        std::uint64_t sentAt = 0, stateSentAt = 0;
    };
    void decide(net::RuleDecisionMsg d);
    void apply(const net::RuleDecisionMsg& d);
    // `progress`: the waypoints, place and icons too (a state newer than
    // any taken), else only what stands in for missed decisions.
    void applyRaceState(const net::RulesMsg& m, const Vec3& carPosition, bool progress);
    net::RulesMsg message(std::uint8_t player, Client& c, bool decisions = true) const;
    std::string nameOf(std::uint8_t id) const;

    Setup m_setup;
    std::unique_ptr<session::RaceReferee> m_referee;
    std::map<std::uint8_t, std::uint32_t> m_carSamples; // host: each car's last sample
    std::map<std::uint8_t, Client> m_clients;
    std::set<std::uint8_t> m_left;
    std::vector<net::RuleDecisionMsg> m_log;
    std::uint32_t m_lastDecision = 0; // client: the newest applied
    std::uint32_t m_lastMessage = 0;  // client: the newest message (reliable)
    std::uint32_t m_lastState = 0;    // client: the newest state taken (either kind)
    std::uint32_t m_evaluated = 0;
    std::optional<std::uint32_t> m_pickupSample;
    std::set<std::uint8_t> m_finished;
    std::map<std::uint8_t, int> m_icons;
    bool m_timedOut = false, m_allCounted = false;
    std::map<std::uint8_t, std::string> m_names;
    std::FILE* m_trace = nullptr;
    double m_frameTime = 0.0;
    std::map<std::uint8_t, std::size_t> m_tracedHits;
    std::size_t m_confirmedHits = 0; // client: the host's count of its car's hits (the trace's RC)
    Stats m_stats;
};

} // namespace mm2::game
