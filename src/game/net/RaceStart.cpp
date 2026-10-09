#include "game/net/RaceStart.h"

#include "core/Log.h"
#include "game/Strings.h"
#include "game/net/NetGame.h"

#include <format>
#include <utility>

namespace mm2::game {

NetRaceStart::Kind NetRaceStart::kindOf(GameMode mode) {
    switch (mode) {
    case GameMode::Cruise: return Kind::Cruise;
    case GameMode::CopsAndRobbers: return Kind::CopsAndRobbers;
    default: return Kind::Race;
    }
}

float NetRaceStart::countdownSeconds(Kind kind) { return kind == Kind::Race ? 2.5f : 0.0f; }

NetRaceStart::Output NetRaceStart::update(float dt, const Input& in) {
    Output out;
    if (m_kind == Kind::Cruise) {
        // mmMultiRoam::Init reports at the end of the load; UpdateGame says
        // "Go!" and lets the car go at once (Session::start).
        out.report = !std::exchange(m_reported, true);
        out.held = false;
        return out;
    }
    if (!m_reported) {
        // State 0 counts 5 s of frames, then sends RaceReady on the next one.
        // A start that comes meanwhile goes first: 0x20f in state 0 starts
        // the countdown at once (and MM2 never sends that RaceReady; OpenMM2
        // still reports, so the others show the car).
        if (m_kind == Kind::Race && !in.startKnown && m_settle <= kSettleSeconds) {
            m_settle += dt;
            return out;
        }
        m_reported = true;
        out.report = true;
    }
    if (!in.startKnown) {
        out.waitingFor = in.othersLoading;
        return out;
    }
    const float countdown = countdownSeconds(m_kind);
    if (!m_startSeen) {
        m_startSeen = true;
        // Loaded after the start (the host stopped waiting for this
        // machine): the whole countdown from now, on its own.
        if (in.secondsToStart <= 0.0) {
            m_own = true;
            m_ownLeft = countdown;
        }
    }
    float toGo = static_cast<float>(in.secondsToStart);
    if (m_own) {
        toGo = m_ownLeft;
        m_ownLeft -= dt;
    }
    out.secondsToGo = toGo;
    if (!m_began && toGo <= countdown) {
        m_began = true;
        out.countdownBegan = true;
    }
    if (toGo <= 0.0f) {
        out.held = false;
        out.went = !std::exchange(m_gone, true);
    }
    return out;
}

NetRaceStart::Output NetRaceStart::update(float dt, NetGame& net) {
    Input in;
    in.startKnown = net.raceStartKnown();
    in.secondsToStart = in.startKnown ? net.secondsToStart() : 0.0;
    in.othersLoading = net.playersLoading();
    Output out = update(dt, in);
    const std::uint32_t race = net.raceNumber();
    if (out.report) {
        net.reportLoaded();
        log::info("race {}: loaded, reported at session time {:.0f}", race, net.frameTime());
    }
    if (out.countdownBegan)
        log::info("race {}: countdown from session time {:.0f}{}", race, net.frameTime(),
                  m_own ? " (on its own: loaded after the start)" : "");
    if (out.went)
        log::info("race {}: GO at session time {:.0f} (the start: {})", race, net.frameTime(),
                  net.raceStartTime());
    // mmGameMulti::GameMessageCB 0x1fa: "<name>" / "has joined" as each other
    // machine's cruise begins (mmMultiRoam::Init's SendGameSet).
    if (m_kind == Kind::Cruise)
        for (const auto& p : net.players())
            if (p.id != net.localId() && net.playerLoaded(p.id) && m_joined.insert(p.id).second)
                out.joined.push_back(p.id);
    return out;
}

std::string NetRaceStart::waitingText(const Strings& strings, int players) {
    const std::string fallback =
        players == 1 ? std::string("Waiting for 1 player") : std::format("Waiting for {} players", players);
    if (players >= 1 && players <= 7)
        return strings.get(30u + static_cast<std::uint32_t>(players), fallback);
    return fallback;
}

} // namespace mm2::game
