#pragma once

// The host's rules in a network race (OpenMM2's own protocol, version 8; see
// docs/multiplayer.md "Rules"). The host simulates every player's car, so it
// decides the rules from its cars: in a race each car's checkpoints, laps,
// finish and time, the finish timeout, the end and the standings; in Cops
// and Robbers the gold's pickups, drops and deliveries, the scores, the new
// places and the limits. MM2's machines each decided their own car's and
// told the others (mmGameMulti::SendPosition's waypoint count,
// SendFinishReq; mmMultiCR's 0x25e, 0x259, 600); the host acknowledged
// finishes and pickups and drew the places (SendFinishAck 0x1f7,
// SendGoldAck 0x25a, SendChangeSet 0x261) and ran the timeouts and limits
// (0x1fe, 0x211, SendLimitReached).
//
// One message, a reliable game event from the host to one player, carries
// what the host decided since its last message to that player (numbered,
// so each decision is shown once) and the whole state as it stands (so a
// player that missed a decision, or joined the race late, still has it).
// The host refuses every rule event a player sends (NetGame's event
// filter), and a player takes this one from the host only.

#include "net/Protocol.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mm2::net {

inline constexpr std::uint16_t kRulesEvent = static_cast<std::uint16_t>(GameEventType::Custom) + 0x40;

inline constexpr std::size_t kMaxRuleDecisions = 8; // per message (more wait for the next)
inline constexpr std::size_t kMaxRuleHits = 256;    // waypoints hit, per message (the newest)
inline constexpr std::uint32_t kRuleDnfMs = 86400000; // MM2's 86400 s: did not finish
inline constexpr float kMaxRulePosition = 16384.0f;
inline constexpr std::int32_t kMaxRuleScore = 1 << 20;

enum class RuleDecision : std::uint8_t {
    Finished,      // `player` finished in `ms` (kRuleDnfMs: did not) (0x1f7)
    TimedOut,      // the race is over for everyone still racing (0x1fe)
    AllCounted,    // every player is counted: the results (0x211)
    GoldTaken,     // `player` has the gold (0x25a)
    GoldDropped,   // `player` lost it at `position` (`flag`: knocked loose by a hit) (0x259)
    GoldDelivered, // `player` delivered it (600)
    NewSet,        // the bank, gold and hideout at new places (0x261)
    Limit,         // the game is over (`flag`: a point limit, `player` and `value` the winner's; else the
                   // time) (SendLimitReached)
    Last = Limit,
};

struct RuleDecisionMsg {
    std::uint32_t seq = 0; // the host's number for it, from 1, the same for every player
    RuleDecision type{};
    std::uint8_t player = kInvalidPlayerId;
    std::uint32_t ms = 0;
    Vec3 position;
    bool flag = false;
    std::int32_t value = 0;
    Vec3 gold, bank, hideout;

    bool operator==(const RuleDecisionMsg&) const = default;
};

struct RulesMsg {
    std::uint32_t race = 0;      // the race's number (a message for another race is dropped)
    std::uint32_t seq = 0;       // this player's messages in the race, from 1
    std::uint32_t evaluated = 0; // the last sample of this player's car the rules have seen
    std::vector<RuleDecisionMsg> decisions;
    bool cops = false; // Cops and Robbers' state follows, else a race's

    // A race: this player's car's waypoints hit, in order, from hit number
    // `firstHit`; its place and the racers (mmGameMulti::UpdateScore on its
    // machine); the other cars' icon numbers as its machine ranks them (0:
    // none); the results so far by time (mmGameMulti::SortResults).
    std::uint32_t firstHit = 0;
    std::vector<std::uint8_t> hits;
    std::uint8_t place = 1, racers = 1;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> icons;
    std::vector<std::pair<std::uint8_t, std::uint32_t>> results;
    bool timedOut = false, allCounted = false;

    // Cops and Robbers: who carries the gold (kInvalidPlayerId: nobody),
    // whether it can be taken, where it is, the places, every player's
    // score, the game over.
    std::uint8_t carrier = kInvalidPlayerId;
    bool goldActive = true;
    Vec3 goldPosition, bank, setGold, hideout;
    std::vector<std::pair<std::uint8_t, std::int32_t>> scores;
    bool over = false;

    bool operator==(const RulesMsg&) const = default;
};

namespace detail {

template <class S>
bool rulePosition(S& s, Vec3& v) {
    s.vec3(v);
    if (std::fabs(v.x) > kMaxRulePosition || std::fabs(v.y) > kMaxRulePosition ||
        std::fabs(v.z) > kMaxRulePosition)
        return s.fail();
    return s.ok();
}

template <class S>
bool rulePlayer(S& s, std::uint8_t& id, bool optional) {
    s.u8(id);
    if (id == kInvalidPlayerId && !optional)
        return s.fail();
    return s.ok();
}

template <class S, class T, class F>
bool ruleList(S& s, std::vector<T>& list, std::size_t max, F each) {
    auto count = static_cast<std::uint32_t>(list.size());
    s.varU32(count);
    if (count > max)
        return s.fail();
    if constexpr (S::kReading)
        list.resize(count);
    for (auto& item : list)
        if (!each(item))
            return s.fail();
    return s.ok();
}

} // namespace detail

template <class S>
bool serialize(S& s, RuleDecisionMsg& d) {
    s.u32(d.seq);
    s.enumeration(d.type, RuleDecision::Last);
    switch (d.type) {
    case RuleDecision::Finished:
        detail::rulePlayer(s, d.player, false);
        s.u32(d.ms);
        if (d.ms > kRuleDnfMs)
            return s.fail();
        break;
    case RuleDecision::TimedOut:
    case RuleDecision::AllCounted: break;
    case RuleDecision::GoldTaken:
    case RuleDecision::GoldDelivered: detail::rulePlayer(s, d.player, false); break;
    case RuleDecision::GoldDropped:
        detail::rulePlayer(s, d.player, false);
        detail::rulePosition(s, d.position);
        s.boolean(d.flag);
        break;
    case RuleDecision::NewSet:
        detail::rulePosition(s, d.gold);
        detail::rulePosition(s, d.bank);
        detail::rulePosition(s, d.hideout);
        break;
    case RuleDecision::Limit:
        s.boolean(d.flag);
        detail::rulePlayer(s, d.player, true);
        s.ranged(d.value, 0, kMaxRuleScore);
        break;
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, RulesMsg& m) {
    s.u32(m.race);
    s.u32(m.seq);
    s.u32(m.evaluated);
    detail::ruleList(s, m.decisions, kMaxRuleDecisions, [&](RuleDecisionMsg& d) { return serialize(s, d); });
    s.boolean(m.cops);
    if (!s.ok())
        return false;
    if (!m.cops) {
        s.varU32(m.firstHit);
        detail::ruleList(s, m.hits, kMaxRuleHits, [&](std::uint8_t& h) {
            s.u8(h);
            return s.ok();
        });
        s.u8(m.place);
        s.u8(m.racers);
        detail::ruleList(s, m.icons, kMaxPlayers, [&](std::pair<std::uint8_t, std::uint8_t>& e) {
            detail::rulePlayer(s, e.first, false);
            s.u8(e.second);
            return s.ok() && e.second <= kMaxPlayers;
        });
        detail::ruleList(s, m.results, kMaxPlayers, [&](std::pair<std::uint8_t, std::uint32_t>& e) {
            detail::rulePlayer(s, e.first, false);
            s.u32(e.second);
            return s.ok() && e.second <= kRuleDnfMs;
        });
        s.boolean(m.timedOut);
        s.boolean(m.allCounted);
        if (m.place < 1 || m.racers < 1 || m.place > kMaxPlayers || m.racers > kMaxPlayers)
            return s.fail();
        return s.ok();
    }
    detail::rulePlayer(s, m.carrier, true);
    s.boolean(m.goldActive);
    detail::rulePosition(s, m.goldPosition);
    detail::rulePosition(s, m.bank);
    detail::rulePosition(s, m.setGold);
    detail::rulePosition(s, m.hideout);
    detail::ruleList(s, m.scores, kMaxPlayers, [&](std::pair<std::uint8_t, std::int32_t>& e) {
        detail::rulePlayer(s, e.first, false);
        s.ranged(e.second, 0, kMaxRuleScore);
        return s.ok();
    });
    s.boolean(m.over);
    return s.ok();
}

} // namespace mm2::net
