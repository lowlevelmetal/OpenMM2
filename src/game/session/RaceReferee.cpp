#include "game/session/RaceReferee.h"

#include <algorithm>

namespace mm2::game::session {
namespace {

WaypointRule ruleFor(GameMode mode) {
    switch (mode) {
    case GameMode::Blitz: return WaypointRule::Blitz;
    case GameMode::Circuit: return WaypointRule::Circuit;
    case GameMode::Checkpoint: return WaypointRule::CheckpointRace;
    default: return WaypointRule::None;
    }
}

} // namespace

RaceReferee::RaceReferee(Config config) : m_config(std::move(config)) {}

void RaceReferee::addPlayer(std::uint8_t id) {
    auto [it, added] = m_players.try_emplace(id);
    if (added)
        it->second.wp.reset(m_config.checkpoints, ruleFor(m_config.mode), m_config.laps, false);
}

void RaceReferee::removePlayer(std::uint8_t id) {
    if (const auto it = m_players.find(id); it != m_players.end())
        it->second.inRace = false;
}

const RaceReferee::Player* RaceReferee::player(std::uint8_t id) const {
    const auto it = m_players.find(id);
    return it == m_players.end() ? nullptr : &it->second;
}

std::vector<RaceReferee::Decision> RaceReferee::takeDecisions() {
    std::vector<Decision> out;
    out.swap(m_decisions);
    return out;
}

void RaceReferee::sample(std::uint8_t id, std::uint32_t seq, const Mat34& car, const Vec3& inertiaBox,
                         bool held) {
    addPlayer(id);
    Player& p = m_players.at(id);
    if (!p.inRace || (p.evaluated != 0 && seq <= p.evaluated))
        return;
    p.placed = true;
    p.position = car.m3;
    p.evaluated = seq;
    // The car races from the first sample its own input lets it go (its
    // player's Go at the shared start; a late loader's own Go).
    if (!p.released) {
        if (held)
            return;
        p.released = true;
        p.releaseSeq = seq;
    }
    if (p.finish)
        return;
    // mmWaypoints::Update, as the car's own machine runs it (that machine
    // once a frame, the host after every sample: the same at 60 frames a
    // second).
    const int index = p.wp.detect(m_config.checkpoints, car, inertiaBox);
    if (index >= 0) {
        std::vector<WaypointStep> steps;
        if (p.wp.apply(m_config.checkpoints, index, car.m3, steps)) {
            p.hits.push_back(static_cast<std::uint8_t>(index));
            p.hitSamples.push_back(seq);
        }
    }
    // mmMultiRace / mmMultiCircuit / mmMultiBlitz::UpdateGame state 3. A
    // held car (the wreck penalty's state 7) is not checked until it goes
    // again.
    if (held)
        return;
    const float seconds = static_cast<float>(seq - p.releaseSeq + 1) * m_config.sampleSeconds;
    if (p.wp.finished) {
        decideFinish(id, p, seconds);
        return;
    }
    if (m_config.mode == GameMode::Blitz && m_config.timeLimit > 0.0f && seconds >= m_config.timeLimit) {
        // Its clock ran out: "Time's up!" and a did-not-finish
        // (SendFinishReq(86400)); the host's own clock running out tells
        // everyone (0x1fe).
        p.wp.stopped = true;
        decideFinish(id, p, kDnf);
        if (id == m_config.host)
            timeOutAll();
    }
}

void RaceReferee::decideFinish(std::uint8_t id, Player& p, float seconds) {
    if (p.finish)
        return;
    p.finish = seconds;
    // mmGameMulti::SortResults: a player is listed once, by time (a later
    // equal time goes after; did-not-finish last).
    if (std::ranges::none_of(m_results, [id](const Result& r) { return r.player == id; })) {
        const auto at =
            std::ranges::find_if(m_results, [seconds](const Result& r) { return r.seconds > seconds; });
        m_results.insert(at, Result{id, seconds});
    }
    m_decisions.push_back({Decision::Kind::Finished, id, seconds});
    // SetTimeoutOn: the first finish arms the finish timeout.
    if (waitsForAll() && m_results.size() < 2 && !m_timedOut && !m_allCounted) {
        m_timeoutOn = true;
        m_timeout = timeout();
    }
}

void RaceReferee::timeOutAll() {
    // 0x1fe: every machine still racing stops ("Race over") and does not
    // finish (SendFinishReq(86400)), which the host counts.
    if (m_timedOut)
        return;
    m_timedOut = true;
    m_timeoutOn = false;
    m_decisions.push_back({Decision::Kind::TimedOut});
    for (auto& [id, p] : m_players) {
        if (!p.inRace || p.finish)
            continue;
        p.wp.stopped = true;
        decideFinish(id, p, kDnf);
    }
}

Vec3 RaceReferee::targetOf(const Player& p) const {
    const int n = static_cast<int>(m_config.checkpoints.size());
    return m_config.checkpoints[static_cast<std::size_t>(std::clamp(p.wp.current, 0, n - 1))].position;
}

void RaceReferee::update(float dt) {
    // The finish timeout (mmMultiRace / mmMultiCircuit::UpdateGame on the
    // host): when it has run out, 0x1fe.
    if (m_timeoutOn && !m_timedOut) {
        m_timeout -= dt;
        if (m_timeout <= 0.0f)
            timeOutAll();
    }
    // 0x211 once every player still in the race is counted (OpenMM2 counts
    // the players still in the race, where MM2 compared the count with the
    // session's players: a player who finished and left does not end the
    // race of one still driving).
    if (waitsForAll() && !m_allCounted && !m_results.empty() &&
        std::ranges::all_of(m_players, [](const auto& e) { return !e.second.inRace || e.second.finish; })) {
        m_allCounted = true;
        m_timeoutOn = false;
        m_decisions.push_back({Decision::Kind::AllCounted});
    }
    // mmGameMulti::UpdateScore on each player's machine: its place among the
    // racers while its waypoints are not done.
    const int n = static_cast<int>(m_config.checkpoints.size());
    if (n < 2)
        return;
    for (auto& [id, p] : m_players) {
        if (!p.inRace || p.wp.finished)
            continue;
        const Vec3 target = targetOf(p);
        const float mine = p.position.dist2(target);
        int place = 1, racers = 1;
        for (const auto& [qid, q] : m_players) {
            if (qid == id || !q.inRace)
                continue;
            const bool done = q.finish.has_value();
            if (!q.placed) {
                // A slot without a car counts only once finished.
                if (done) {
                    ++racers;
                    ++place;
                }
                continue;
            }
            ++racers;
            if (p.wp.count < q.wp.count || done)
                ++place;
            else if (p.wp.count == q.wp.count && q.position.dist2(target) < mine)
                ++place;
        }
        p.place = place;
        p.racers = racers;
    }
}

int RaceReferee::iconPlace(std::uint8_t viewer, std::uint8_t car) const {
    // mmGameMulti::UpdateScore's second half on `viewer`'s machine: a car's
    // IconIndex is 1 + the other cars ahead of it (more waypoints passed,
    // finished, or level and nearer its next waypoint) + the viewer's own
    // car when that is ahead (more waypoints, its waypoints done, or level
    // and nearer the viewer's own target).
    const int n = static_cast<int>(m_config.checkpoints.size());
    const Player* p = player(car);
    if (n < 2 || !p || !p->inRace || !p->placed || p->finish || viewer == car)
        return 0;
    const Vec3 wp = m_config.checkpoints[static_cast<std::size_t>(p->wp.count % n)].position;
    const float own = p->position.dist2(wp);
    int place = 1;
    for (const auto& [qid, q] : m_players) {
        if (qid == car || qid == viewer || !q.inRace)
            continue;
        const bool done = q.finish.has_value();
        if (!q.placed) {
            if (done)
                ++place;
            continue;
        }
        if (q.wp.count > p->wp.count || done)
            ++place;
        else if (q.wp.count == p->wp.count && q.position.dist2(wp) < own)
            ++place;
    }
    if (const Player* v = player(viewer); v && v->placed) {
        if (p->wp.count < v->wp.count || v->wp.finished) {
            ++place;
        } else if (p->wp.count == v->wp.count) {
            const Vec3 target = targetOf(*v);
            if (v->position.dist2(target) < p->position.dist2(target))
                ++place;
        }
    }
    return place;
}

} // namespace mm2::game::session
