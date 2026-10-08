#include "game/session/CopsAndRobbers.h"

#include "city/Race.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mm2::game::session {
namespace {

// mmMultiCR::FondleCarMass: gold weight option -> extra kg and the
// carrier's throttle cap (mmGame::UpdateSteeringBrakes, multiplayer, above
// first gear).
constexpr std::array<float, 3> kGoldMassKg{0.0f, 100.0f, 200.0f};
constexpr std::array<float, 3> kGoldThrottle{1.0f, 0.9f, 0.81f};

// mmMultiCR::UpdateTimeWarning: minutes left and their thresholds.
constexpr std::array<int, 5> kWarningMinutes{20, 15, 10, 5, 1};

std::uint32_t nextRandom(std::uint32_t& rng) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7fffu;
}

} // namespace

std::optional<CrLocations> loadCrLocations(const vfs::Vfs& vfs, const std::string& raceDir) {
    auto bytes = vfs.readAll("race/" + raceDir + "/multicopwaypoints.csv");
    if (!bytes)
        return std::nullopt;
    auto pts = city::parseWaypoints(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    // mmMultiCR::LoadCSV needs at least three places.
    if (!pts || pts->size() < 3)
        return std::nullopt;
    CrLocations loc;
    for (const auto& p : *pts)
        loc.points.push_back(p.position);
    return loc;
}

CopsAndRobbers::CopsAndRobbers(const CrSettings& settings, const CrLocations& locations)
    : m_settings(settings), m_locations(locations), m_rng(settings.seed ? settings.seed : 1) {
    m_timeLeft = settings.timeLimitSeconds;
    m_lastWarning = settings.timeLimitSeconds;
    newSet();
}

void CopsAndRobbers::addCar(int id, CrTeam team) {
    m_cars.emplace_back(id, team);
    m_scores.emplace_back(id, 0);
}

CrTeam CopsAndRobbers::teamOf(int id) const {
    for (const auto& [cid, team] : m_cars)
        if (cid == id)
            return team;
    return CrTeam::Robber;
}

Vec3 CopsAndRobbers::deliveryTarget(CrTeam team) const { return teamZero(team) ? m_set.bank : m_set.hideout; }

int CopsAndRobbers::score(CrTeam team) const {
    // mmMultiCR::UpdateHUD: team totals are the members' scores.
    int total = 0;
    for (const auto& [id, s] : m_scores)
        if (teamZero(teamOf(id)) == teamZero(team))
            total += s;
    return total;
}

int CopsAndRobbers::playerScore(int id) const {
    for (const auto& [cid, s] : m_scores)
        if (cid == id)
            return s;
    return 0;
}

float CopsAndRobbers::carrierExtraMassKg() const {
    return kGoldMassKg[static_cast<std::size_t>(std::clamp(m_settings.goldMass, 0, 2))];
}

float CopsAndRobbers::carrierThrottleCap() const {
    return kGoldThrottle[static_cast<std::size_t>(std::clamp(m_settings.goldMass, 0, 2))];
}

Vec3 CopsAndRobbers::randomPoint() {
    // GetRandomPoints' picker (an unnamed helper): a coin flip between an AI
    // intersection and a row of the pool other than the last.
    const auto& pool = m_locations.points;
    const bool coin = (nextRandom(m_rng) & 1u) != 0;
    if ((pool.size() < 2 || coin) && m_settings.randomIntersection)
        if (auto p = m_settings.randomIntersection())
            return *p;
    if (pool.empty())
        return {};
    if (pool.size() == 1)
        return pool.front();
    return pool[nextRandom(m_rng) % (pool.size() - 1)];
}

void CopsAndRobbers::newSet() {
    // mmMultiCR::GetNewSet / GetRandomPoints: gold, then a different bank,
    // then a hideout different from both.
    m_set.gold = randomPoint();
    m_set.bank = randomPoint();
    for (int i = 0; i < 64 && m_set.bank == m_set.gold; ++i)
        m_set.bank = randomPoint();
    m_set.hideout = randomPoint();
    for (int i = 0; i < 64 && (m_set.hideout == m_set.bank || m_set.hideout == m_set.gold); ++i)
        m_set.hideout = randomPoint();
    m_goldPos = m_set.gold;
    m_carrier = -1;
    m_events.push_back({EventType::NewSet});
}

void CopsAndRobbers::drop(int carId, const Vec3& at, bool toSpawn) {
    // mmMultiCR::DropGold: where the carrier is, unless that is off the
    // roads or the car went into the water; then back to the set's place.
    const bool stays = !toSpawn && (!m_settings.canDropAt || m_settings.canDropAt(at));
    m_goldPos = stays ? at : m_set.gold;
    m_carrier = -1;
    m_events.push_back({EventType::GoldDropped, carId});
}

void CopsAndRobbers::score(int id, int points) {
    // mmMultiCR::Score.
    for (auto& [cid, s] : m_scores)
        if (cid == id)
            s += points;
    m_scored = true;
}

void CopsAndRobbers::checkLimits() {
    // mmMultiCR::UpdateLimit (point limit): a player's score in
    // Free-For-All, a team's otherwise.
    if (m_settings.pointLimit <= 0 || !m_scored)
        return;
    if (m_settings.mode == CopsAndRobbersMode::FreeForAll) {
        for (const auto& [id, s] : m_scores) {
            if (s >= m_settings.pointLimit) {
                m_over = true;
                m_events.push_back({EventType::PointLimit, id, s});
                return;
            }
        }
        return;
    }
    for (CrTeam t : {CrTeam::Cop, CrTeam::Robber}) {
        if (score(t) >= m_settings.pointLimit) {
            m_over = true;
            m_events.push_back({EventType::PointLimit, -1, score(t)});
            return;
        }
    }
}

void CopsAndRobbers::update(float dt, const std::vector<Car>& cars, const std::vector<Impact>& impacts) {
    if (m_over)
        return;
    auto find = [&](int id) -> const Car* {
        for (const auto& c : cars)
            if (c.id == id)
                return &c;
        return nullptr;
    };
    auto locked = [&](int id) {
        for (const auto& [cid, t] : m_lockout)
            if (cid == id && t > 0.0f)
                return true;
        return false;
    };
    auto lock = [&](int id, float seconds) {
        for (auto& [cid, t] : m_lockout)
            if (cid == id) {
                t = std::max(t, seconds);
                return;
            }
        m_lockout.emplace_back(id, seconds);
    };
    for (auto& [cid, t] : m_lockout)
        t -= dt;

    // mmMultiCR::ImpactCallback runs during the physics step, before the
    // frame's UpdateGame: a hard enough hit from another car knocks the
    // gold loose; the carrier cannot take it back for 2 s (state 7).
    bool droppedByImpact = false;
    if (const Car* c = m_carrier >= 0 ? find(m_carrier) : nullptr) {
        for (const auto& im : impacts) {
            const int other = im.a == m_carrier ? im.b : (im.b == m_carrier ? im.a : -1);
            if (other < 0 || im.impulse < kStealImpulse || !find(other))
                continue;
            lock(c->id, kDropLockout);
            drop(c->id, c->position, false);
            droppedByImpact = true;
            break;
        }
    }

    // UpdateGame state 4: wrecked cars sit out 5 s (state 6); a wrecked
    // carrier drops the gold where it is.
    for (const auto& c : cars)
        if (c.wrecked)
            lock(c.id, kWreckPenalty);

    // Then UpdateLimit (the time running out, the point limit, checked
    // before this frame's deliveries) and UpdateTimeWarning ("N minutes
    // remaining" as each mark is passed).
    if (m_settings.timeLimitSeconds > 0.0f) {
        m_timeLeft = std::max(0.0f, m_timeLeft - dt);
        if (m_timeLeft < 0.1f) {
            m_over = true;
            m_events.push_back({EventType::TimeUp});
            return;
        }
    }
    checkLimits();
    if (m_over)
        return;
    if (m_settings.timeLimitSeconds > 0.0f) {
        for (int minutes : kWarningMinutes) {
            const float mark = static_cast<float>(minutes) * 60.0f;
            if (m_timeLeft < mark && mark < m_lastWarning) {
                m_lastWarning = m_timeLeft;
                m_events.push_back({EventType::TimeWarning, -1, minutes});
            }
        }
    }

    // UpdateGold, then UpdateBank / UpdateHideout.
    if (m_carrier < 0) {
        // mmMultiCR::UpdateGold: within 5 m of the gold picks it up. Each
        // machine tests only its own car and the others learn of a drop from
        // the carrier's SendGoldDrop message, so nobody takes the gold in the
        // frame it was knocked loose (inferred from that message round trip;
        // this all-in-one simulation has no latency of its own).
        for (const auto& c : cars) {
            if (droppedByImpact)
                break;
            if (c.wrecked || locked(c.id))
                continue;
            if (c.position.dist2(m_goldPos) < kGoldRadius * kGoldRadius) {
                m_carrier = c.id;
                score(c.id, kPickupPoints);
                m_events.push_back({EventType::GoldTaken, c.id, kPickupPoints});
                break;
            }
        }
    } else if (const Car* c = find(m_carrier); !c) {
        drop(m_carrier, m_goldPos, true);
    } else if (c->inWater) {
        drop(c->id, c->position, true);
    } else if (c->wrecked) {
        drop(c->id, c->position, false);
    } else {
        // UpdateGold: the carried gold rides 2 m above the carrier.
        m_goldPos = c->position + Vec3{0.0f, 2.0f, 0.0f};
        // mmMultiCR::UpdateBank / UpdateHideout: delivered within 12 m.
        if (c->position.dist2(deliveryTarget(teamOf(c->id))) < kBaseRadius * kBaseRadius) {
            score(c->id, kDeliveryPoints);
            m_events.push_back({EventType::GoldDelivered, c->id, kDeliveryPoints});
            newSet();
        }
    }
}

std::vector<CopsAndRobbers::Event> CopsAndRobbers::takeEvents() {
    std::vector<Event> out;
    out.swap(m_events);
    return out;
}

} // namespace mm2::game::session
