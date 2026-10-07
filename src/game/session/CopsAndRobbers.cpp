#include "game/session/CopsAndRobbers.h"

#include "city/Race.h"

#include <algorithm>
#include <cmath>

namespace mm2::game::session {

std::optional<CrLocations> loadCrLocations(const vfs::Vfs& vfs, const std::string& raceDir) {
    auto bytes = vfs.readAll("race/" + raceDir + "/multicopwaypoints.csv");
    if (!bytes)
        return std::nullopt;
    auto pts = city::parseWaypoints(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!pts || pts->size() < 3)
        return std::nullopt;
    CrLocations loc;
    loc.bank = (*pts)[0].position;
    loc.hideout = (*pts)[1].position;
    for (std::size_t i = 2; i < pts->size(); ++i)
        loc.gold.push_back((*pts)[i].position);
    return loc;
}

CopsAndRobbers::CopsAndRobbers(const CrSettings& settings, const CrLocations& locations)
    : m_settings(settings), m_locations(locations), m_rng(settings.seed ? settings.seed : 1) {
    m_timeLeft = settings.timeLimitSeconds;
    respawnGold();
}

void CopsAndRobbers::addCar(int id, CrTeam team) {
    m_cars.emplace_back(id, team);
    m_playerScores.emplace_back(id, 0);
}

CrTeam CopsAndRobbers::teamOf(int id) const {
    for (const auto& [cid, team] : m_cars)
        if (cid == id)
            return team;
    return CrTeam::Robber;
}

Vec3 CopsAndRobbers::deliveryTarget(CrTeam team) const {
    // Cops return the gold to the bank; robbers take it to the hideout. Robber
    // teams: red uses the hideout, blue the bank (inferred).
    switch (team) {
    case CrTeam::Cop:
    case CrTeam::Blue: return m_locations.bank;
    default: return m_locations.hideout;
    }
}

int CopsAndRobbers::score(CrTeam team) const { return m_teamScore[static_cast<int>(team)]; }

int CopsAndRobbers::playerScore(int id) const {
    for (const auto& [cid, s] : m_playerScores)
        if (cid == id)
            return s;
    return 0;
}

void CopsAndRobbers::respawnGold() {
    m_carrier = -1;
    if (m_locations.gold.empty()) {
        m_goldPos = m_locations.bank;
        return;
    }
    // MM1 GetRandomIndex: a random spot, never the same twice in a row.
    m_rng = m_rng * 1664525u + 1013904223u;
    std::size_t next = (m_rng >> 8) % m_locations.gold.size();
    if (next == m_goldIndex && m_locations.gold.size() > 1)
        next = (next + 1) % m_locations.gold.size();
    m_goldIndex = next;
    m_goldPos = m_locations.gold[next];
}

void CopsAndRobbers::scoreFor(int id) {
    const CrTeam team = teamOf(id);
    ++m_teamScore[static_cast<int>(team)];
    for (auto& [cid, s] : m_playerScores)
        if (cid == id)
            ++s;
    m_events.push_back({EventType::GoldDelivered, id, m_teamScore[static_cast<int>(team)]});
    if (m_settings.pointLimit > 0) {
        const int best = m_settings.mode == CopsAndRobbersMode::FreeForAll ? playerScore(id)
                                                                          : m_teamScore[static_cast<int>(team)];
        if (best >= m_settings.pointLimit) {
            m_over = true;
            m_events.push_back({EventType::PointLimit, id, best});
        }
    }
}

void CopsAndRobbers::update(float dt, std::vector<Car> cars, const std::vector<Impact>& impacts) {
    if (m_over)
        return;
    auto find = [&](int id) -> const Car* {
        for (const auto& c : cars)
            if (c.id == id)
                return &c;
        return nullptr;
    };

    // Time limit with warnings at 20/15/10/5/1 minutes left (strings 138-142).
    if (m_settings.timeLimitSeconds > 0.0f) {
        m_timeLeft -= dt;
        for (int minutes : {20, 15, 10, 5, 1}) {
            if (m_timeLeft <= minutes * 60.0f && m_timeLeft > minutes * 60.0f - 1.0f && m_lastWarning != minutes &&
                m_settings.timeLimitSeconds > minutes * 60.0f) {
                m_lastWarning = minutes;
                m_events.push_back({EventType::TimeWarning, -1, minutes});
            }
        }
        if (m_timeLeft <= 0.0f) {
            m_timeLeft = 0.0f;
            m_over = true;
            m_events.push_back({EventType::TimeUp});
            return;
        }
    }

    if (m_carrier >= 0) {
        const Car* carrier = find(m_carrier);
        if (!carrier || carrier->wrecked) {
            // Wrecked carriers drop the gold where they stand (DropGold).
            if (carrier)
                m_goldPos = carrier->position;
            m_events.push_back({EventType::GoldDropped, m_carrier});
            m_carrier = -1;
        } else {
            m_goldPos = carrier->position;
            // Steal on impact (mmMultiCR::ImpactCallback).
            for (const auto& im : impacts) {
                if (im.impulse < m_settings.stealImpulse)
                    continue;
                const int other = im.a == m_carrier ? im.b : (im.b == m_carrier ? im.a : -1);
                if (other < 0)
                    continue;
                const Car* thief = find(other);
                if (!thief || thief->wrecked)
                    continue;
                m_carrier = other;
                m_events.push_back({EventType::GoldStolen, other});
                break;
            }
            // Delivery.
            if (const Car* c = find(m_carrier)) {
                const Vec3 target = deliveryTarget(teamOf(m_carrier));
                if (std::sqrt(sq(c->position.x - target.x) + sq(c->position.z - target.z)) < m_settings.deliverRadius) {
                    scoreFor(m_carrier);
                    respawnGold();
                }
            }
        }
        return;
    }

    // Free gold: the first car to reach it takes it. Cops in Cops vs
    // Robbers pick up loose gold too and return it to the bank.
    for (const auto& c : cars) {
        if (c.wrecked)
            continue;
        if (std::sqrt(sq(c.position.x - m_goldPos.x) + sq(c.position.z - m_goldPos.z)) < m_settings.pickupRadius) {
            m_carrier = c.id;
            m_events.push_back({EventType::GoldTaken, c.id});
            break;
        }
    }
}

std::vector<CopsAndRobbers::Event> CopsAndRobbers::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

} // namespace mm2::game::session
