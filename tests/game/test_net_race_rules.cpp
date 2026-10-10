// The rules of a network race decided by the host (OpenMM2: the host is the
// authority, docs/multiplayer.md "Rules"): the host's referee
// (game::session::RaceReferee), a client's predicted checkpoints confirmed
// or corrected by the host's word (Session's net rules), the message that
// carries it (game::NetRules, net/RulesState.h), and Cops and Robbers' rules
// on the host with a client's predicted pickup.
#include "TestData.h"
#include "city/CityData.h"
#include "game/Strings.h"
#include "game/net/NetRules.h"
#include "game/session/CopsAndRobbers.h"
#include "game/session/Gate.h"
#include "game/session/RaceReferee.h"
#include "game/session/Session.h"
#include "game/session/Waypoints.h"
#include "net/VehicleDamage.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <functional>
#include <thread>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

constexpr float kStep = 1.0f / 60.0f;

// A car facing -Z (heading 0) at `pos`.
Mat34 carAt(const Vec3& pos) {
    Mat34 m = Mat34::identity();
    m.m3 = pos;
    return m;
}

// A straight course along -Z: the start at z = 0, then a gate every 100 m.
std::vector<Checkpoint> straightCourse(int gates) {
    std::vector<Checkpoint> cps;
    for (int i = 0; i <= gates; ++i)
        cps.push_back(makeCheckpoint({0.0f, 0.0f, -100.0f * static_cast<float>(i)}, 0.0f, 10.0f));
    return cps;
}

const Vec3 kBox{2.0f, 1.0f, 4.0f};

// A car driven down the course, 1 m a sample, from z = `from`.
struct Car {
    std::uint8_t id = 0;
    std::uint32_t seq = 0;
    float z = 5.0f;
    void sample(RaceReferee& r, bool held, float metres = 1.0f) {
        ++seq;
        if (!held)
            z -= metres;
        r.sample(id, seq, carAt({0.0f, 0.0f, z}), kBox, held);
    }
};

std::vector<RaceReferee::Decision> drain(RaceReferee& r) { return r.takeDecisions(); }

} // namespace

// --- The tracker -----------------------------------------------------------------------

TEST(NetRaceRules, TrackerAppliesARecordedHitAsItsGateTestDoes) {
    // A car's hits applied again from mmWaypoints::Reset (a client taking
    // the host's word) give the same waypoints as the gate tests did.
    const auto cps = straightCourse(4);
    WaypointTracker live, replay;
    live.reset(cps, WaypointRule::CheckpointRace, 0, false);
    replay.reset(cps, WaypointRule::CheckpointRace, 0, false);
    std::vector<int> hits;
    for (float z = 5.0f; z > -420.0f; z -= 1.0f) {
        std::vector<WaypointStep> steps;
        const int index = live.detect(cps, carAt({0, 0, z}), kBox);
        if (index >= 0 && live.apply(cps, index, {0, 0, z}, steps))
            hits.push_back(index);
    }
    ASSERT_EQ(hits, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_TRUE(live.finished);
    for (const int h : hits) {
        std::vector<WaypointStep> steps;
        EXPECT_TRUE(replay.apply(cps, h, {}, steps));
    }
    EXPECT_EQ(replay.cleared, live.cleared);
    EXPECT_EQ(replay.count, live.count);
    EXPECT_TRUE(replay.finished);
    // The rule refuses what it would not take: a waypoint already cleared,
    // the finish before the last checkpoint.
    WaypointTracker early;
    early.reset(cps, WaypointRule::CheckpointRace, 0, false);
    std::vector<WaypointStep> steps;
    EXPECT_FALSE(early.apply(cps, 4, {}, steps));
    EXPECT_TRUE(early.apply(cps, 2, {}, steps));
    EXPECT_FALSE(early.apply(cps, 2, {}, steps));
}

// --- The host's referee ------------------------------------------------------------------

TEST(NetRaceRules, RefereeTimesEachCarFromItsOwnRelease) {
    // A client's car is released at its own Go, which the host applies a
    // trip later: both cars drive the same course alike and get the same
    // time, whatever their samples' numbers.
    RaceReferee::Config c;
    c.mode = GameMode::Checkpoint;
    c.checkpoints = straightCourse(3);
    RaceReferee r(c);
    r.addPlayer(0);
    r.addPlayer(1);
    Car host{0}, client{1};
    client.seq = 500; // a client numbers its own samples
    for (int i = 0; i < 400; ++i) {
        host.sample(r, i < 10);
        client.sample(r, i < 30);
        r.update(kStep);
    }
    const auto* h = r.player(0);
    const auto* k = r.player(1);
    ASSERT_TRUE(h && k);
    EXPECT_EQ(h->hits, (std::vector<std::uint8_t>{1, 2, 3}));
    EXPECT_EQ(k->hits, h->hits);
    ASSERT_TRUE(h->finish && k->finish);
    EXPECT_FLOAT_EQ(*h->finish, *k->finish);
    // The finish line is 300 m from the start, the car's nose 2.5 m ahead:
    // the car crosses it in its sample 300 - ... (1 m a sample from z = 5).
    EXPECT_NEAR(*h->finish, static_cast<float>(h->hitSamples.back() - 10) * kStep, 1e-4f);
    const auto& results = r.results();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].player, 0); // an equal time later goes after
    // The first finish arms the timeout; both counted ends the race.
    const auto d = drain(r);
    EXPECT_TRUE(std::ranges::any_of(
        d, [](const auto& e) { return e.kind == RaceReferee::Decision::Kind::AllCounted; }));
    EXPECT_FALSE(r.timedOut());
}

TEST(NetRaceRules, RefereeTimesTheRaceOutAFinishLater) {
    // mmMultiRace: 60 s after the first finish, 0x1fe: everyone still racing
    // does not finish, and the race is counted (0x211).
    RaceReferee::Config c;
    c.mode = GameMode::Checkpoint;
    c.checkpoints = straightCourse(2);
    RaceReferee r(c);
    r.addPlayer(0);
    r.addPlayer(1);
    r.addPlayer(2); // still loading: waited for, without a car
    Car a{0}, b{1};
    for (int i = 0; i < 400 && !r.player(0)->finish; ++i) {
        a.sample(r, false);
        b.sample(r, i > 50, 0.2f); // stops after 50 samples
        r.update(kStep);
    }
    ASSERT_TRUE(r.player(0)->finish);
    EXPECT_FALSE(r.player(1)->finish);
    drain(r);
    for (int i = 0; i < 59 * 60; ++i)
        r.update(kStep);
    EXPECT_FALSE(r.timedOut());
    for (int i = 0; i < 2 * 60; ++i)
        r.update(kStep);
    EXPECT_TRUE(r.timedOut());
    const auto d = drain(r);
    ASSERT_GE(d.size(), 4u);
    EXPECT_EQ(d[0].kind, RaceReferee::Decision::Kind::TimedOut);
    EXPECT_EQ(d[1].player, 1);
    EXPECT_EQ(d[1].seconds, RaceReferee::kDnf);
    EXPECT_EQ(d[2].player, 2);
    EXPECT_EQ(d.back().kind, RaceReferee::Decision::Kind::AllCounted);
    ASSERT_EQ(r.results().size(), 3u);
    EXPECT_EQ(r.results()[0].player, 0);
    // A circuit waits 120 s.
    RaceReferee::Config cc = c;
    cc.mode = GameMode::Circuit;
    cc.laps = 1;
    RaceReferee circuit(cc);
    EXPECT_EQ(circuit.config().mode, GameMode::Circuit);
}

TEST(NetRaceRules, RefereeCountsTheLapsOfACircuitInOrder) {
    RaceReferee::Config c;
    c.mode = GameMode::Circuit;
    c.checkpoints = straightCourse(2); // 0, 1, 2 in order; 0 completes a lap
    c.laps = 2;
    RaceReferee r(c);
    r.addPlayer(0);
    Car a{0};
    auto lap = [&] {
        a.z = 5.0f;
        for (int i = 0; i < 230; ++i)
            a.sample(r, false);
    };
    lap(); // 1, 2 (gate 0 is behind the car)
    a.z = 30.0f;
    for (int i = 0; i < 260; ++i) // 0 (lap 1), 1, 2
        a.sample(r, false);
    EXPECT_EQ(r.player(0)->wp.lap, 1);
    a.z = 30.0f;
    for (int i = 0; i < 40; ++i) // 0: lap 2, the finish
        a.sample(r, false);
    EXPECT_EQ(r.player(0)->hits, (std::vector<std::uint8_t>{1, 2, 0, 1, 2, 0}));
    EXPECT_TRUE(r.player(0)->finish);
    // Out of order a gate does not count (the target is the next one).
    RaceReferee r2(c);
    r2.addPlayer(0);
    Car b{0};
    b.z = -150.0f; // skips gate 1
    for (int i = 0; i < 100; ++i)
        b.sample(r2, false);
    EXPECT_TRUE(r2.player(0)->hits.empty());
}

TEST(NetRaceRules, RefereeEndsABlitzOnEachCarsClockAndTheHosts) {
    RaceReferee::Config c;
    c.mode = GameMode::Blitz;
    c.checkpoints = straightCourse(3);
    c.timeLimit = 2.0f;
    c.host = 0;
    RaceReferee r(c);
    r.addPlayer(0);
    r.addPlayer(1);
    r.addPlayer(2);
    Car host{0}, early{1}, late{2};
    for (int i = 0; i < 100; ++i) {
        early.sample(r, false, 0.1f);
        r.update(kStep);
    }
    for (int i = 0; i < 30; ++i) {
        early.sample(r, false, 0.1f);
        host.sample(r, i < 20, 0.1f);
        late.sample(r, i < 25, 0.1f);
        r.update(kStep);
    }
    // The early car's clock ran out (120 samples): it did not finish.
    ASSERT_TRUE(r.player(1)->finish);
    EXPECT_EQ(*r.player(1)->finish, RaceReferee::kDnf);
    EXPECT_FALSE(r.timedOut());
    drain(r);
    for (int i = 0; i < 110; ++i) {
        host.sample(r, false, 0.1f);
        late.sample(r, false, 0.1f);
        r.update(kStep);
    }
    // The host's ran out: everyone still racing is out (0x1fe).
    EXPECT_TRUE(r.timedOut());
    ASSERT_TRUE(r.player(2)->finish);
    EXPECT_EQ(*r.player(2)->finish, RaceReferee::kDnf);
    // A Blitz does not wait for everyone (no 0x211).
    EXPECT_FALSE(r.allCounted());
}

TEST(NetRaceRules, RefereeRanksEveryCarAsItsMachineWould) {
    RaceReferee::Config c;
    c.mode = GameMode::Checkpoint;
    c.checkpoints = straightCourse(4);
    RaceReferee r(c);
    for (std::uint8_t id : {0, 1, 2})
        r.addPlayer(id);
    Car a{0}, b{1}, d{2};
    a.z = -150.0f; // has cleared nothing (it started past gate 1), and stops
    for (int i = 0; i < 160; ++i) {
        a.sample(r, i > 0);
        b.sample(r, false, 1.0f); // clears 1 at about z = -97
        d.sample(r, false, 0.5f); // at z = -75
    }
    r.update(kStep);
    // b: one waypoint passed, ahead of both.
    EXPECT_EQ(r.player(1)->place, 1);
    EXPECT_EQ(r.player(1)->racers, 3);
    EXPECT_EQ(r.iconPlace(0, 1), 1);
    // a and d level at the start's count: d is 25 m from gate 1, a 51 m.
    EXPECT_EQ(r.player(2)->place, 2);
    EXPECT_EQ(r.player(0)->place, 3);
    EXPECT_EQ(r.iconPlace(1, 2), 2);
    EXPECT_EQ(r.iconPlace(1, 0), 3);
    // On d's machine a's icon: d itself is ahead of it, b is.
    EXPECT_EQ(r.iconPlace(2, 0), 3);
    // Nobody's own car gets an icon; a player who left has none and no place.
    EXPECT_EQ(r.iconPlace(1, 1), 0);
    r.removePlayer(2);
    r.update(kStep);
    EXPECT_EQ(r.iconPlace(1, 2), 0);
    EXPECT_EQ(r.player(0)->racers, 2);
}

TEST(NetRaceRules, RefereeNamesTheCheckpointTheWaterPutsACarBackAt) {
    // mmGameMulti::HitWaterHandler puts the car back at the last checkpoint
    // it cleared (mmWaypoints' respawn point): the start until it clears one.
    RaceReferee::Config c;
    c.mode = GameMode::Checkpoint;
    c.checkpoints = straightCourse(3);
    RaceReferee r(c);
    r.addPlayer(1);
    EXPECT_EQ(r.lastCleared(1), 0);
    Car a{1};
    for (int i = 0; i < 120; ++i)
        a.sample(r, false); // clears gate 1
    ASSERT_EQ(r.player(1)->hits, (std::vector<std::uint8_t>{1}));
    EXPECT_EQ(r.lastCleared(1), 1);
    EXPECT_EQ(r.lastCleared(7), std::nullopt); // nobody's
}

// --- Cops and Robbers on the host -------------------------------------------------------

namespace {

CrLocations crPlaces() {
    CrLocations loc;
    for (int i = 0; i < 6; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    return loc;
}

} // namespace

TEST(NetRaceRules, HostRunsCopsAndRobbersForEveryCar) {
    CrSettings st;
    st.mode = CopsAndRobbersMode::FreeForAll;
    st.seed = 7;
    CopsAndRobbers host(st, crPlaces());
    host.addCar(0, CrTeam::Robber);
    host.addCar(1, CrTeam::Robber);
    using Type = CopsAndRobbers::Message::Type;
    // Player 1's car (simulated by the host) reaches the gold: granted.
    std::vector<CopsAndRobbers::Car> cars{{0, CrTeam::Robber, {5000, 0, 5000}, false, false},
                                          {1, CrTeam::Robber, host.set().gold, false, false}};
    auto out = host.updateHost(0.1f, cars, {});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, Type::GoldTaken);
    EXPECT_EQ(out[0].car, 1);
    EXPECT_EQ(host.goldCarrier(), 1);
    EXPECT_EQ(host.playerScore(1), CopsAndRobbers::kPickupPoints);
    // A hit from the host's car on the carrier knocks it loose there and
    // locks the carrier out for 2 s; nobody takes it in that frame.
    cars[0].position = cars[1].position = {320, 0, 10};
    out = host.updateHost(0.1f, cars, {{1, 0, 300.0f}});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, Type::GoldDropped);
    EXPECT_TRUE(out[0].knocked);
    EXPECT_EQ(host.goldCarrier(), -1);
    // The next frame the host's car takes it (player 1 is locked out).
    out = host.updateHost(0.1f, cars, {});
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].car, 0);
    // A weak hit, or one of another car's own, does not.
    EXPECT_TRUE(host.updateHost(0.1f, cars, {{0, 1, 100.0f}, {1, 0, 900.0f}}).empty());
    // The carrier at its base delivers; the host draws the next places.
    cars[0].position = host.deliveryTarget(CrTeam::Robber);
    out = host.updateHost(0.1f, cars, {});
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].type, Type::GoldDelivered);
    EXPECT_EQ(out[1].type, Type::NewSet);
    EXPECT_EQ(host.playerScore(0), CopsAndRobbers::kPickupPoints + CopsAndRobbers::kDeliveryPoints);
    // A wrecked car sits out: it cannot take the gold.
    cars[0].position = {5000, 0, 5000};
    cars[1].position = host.set().gold;
    cars[1].wrecked = true;
    EXPECT_TRUE(host.updateHost(0.1f, cars, {}).empty());
}

TEST(NetRaceRules, AClientsPredictedPickupIsConfirmedOrUndone) {
    CrSettings st;
    st.limitsFromHost = true;
    st.seed = 7;
    using E = CopsAndRobbers::EventType;
    auto client = [&] {
        CopsAndRobbers c(st, crPlaces());
        c.addCar(0, CrTeam::Robber);
        c.addCar(1, CrTeam::Robber);
        c.takeEvents(); // the first places
        return c;
    };
    // Confirmed: shown once.
    CopsAndRobbers c = client();
    const CopsAndRobbers::Car me{1, CrTeam::Robber, c.set().gold, false, false};
    EXPECT_TRUE(c.updatePredicted(0.1f, me, {me}, 2));
    EXPECT_EQ(c.goldCarrier(), 1);
    auto ev = c.takeEvents();
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].type, E::GoldTaken);
    EXPECT_FALSE(c.updatePredicted(0.1f, me, {me}, 2)); // not again
    c.applyHost({CopsAndRobbers::Message::Type::GoldTaken, 1}, 1);
    EXPECT_TRUE(c.takeEvents().empty());
    EXPECT_FALSE(c.pickupPending());
    // Another car had it: undone, and that car's pickup shown.
    CopsAndRobbers d = client();
    EXPECT_TRUE(d.updatePredicted(0.1f, me, {me}, 2));
    d.takeEvents();
    d.applyHost({CopsAndRobbers::Message::Type::GoldTaken, 0}, 1);
    ev = d.takeEvents();
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[0].type, E::PickupUndone);
    EXPECT_EQ(ev[1].type, E::GoldTaken);
    EXPECT_EQ(ev[1].car, 0);
    EXPECT_EQ(d.goldCarrier(), 0);
    EXPECT_EQ(d.playerScore(1), 0);
    // The host never granted it: the state undoes it once the host has seen
    // the car long enough, not before.
    CopsAndRobbers e = client();
    EXPECT_TRUE(e.updatePredicted(0.1f, me, {me}, 2));
    e.takeEvents();
    CopsAndRobbers::State hostState;
    hostState.carrier = -1;
    hostState.goldPosition = e.set().gold;
    hostState.set = e.set();
    e.adopt(hostState, 1, false);
    EXPECT_EQ(e.goldCarrier(), 1);
    e.adopt(hostState, 1, true);
    EXPECT_EQ(e.goldCarrier(), -1);
    ev = e.takeEvents();
    ASSERT_EQ(ev.size(), 1u);
    EXPECT_EQ(ev[0].type, E::PickupUndone);
    // Alone in the game a car takes nothing (UpdateGold's other players).
    CopsAndRobbers f = client();
    EXPECT_FALSE(f.updatePredicted(0.1f, me, {me}, 1));
    // With another car at the gold too, it waits for the host's word.
    const CopsAndRobbers::Car other{0, CrTeam::Robber, f.set().gold + Vec3{2.0f, 0.0f, 0.0f}, false, false};
    EXPECT_FALSE(f.updatePredicted(0.1f, me, {me, other}, 2));
    EXPECT_EQ(f.goldCarrier(), -1);
}

// --- The message ----------------------------------------------------------------------------

TEST(NetRaceRules, TheHostRefusesEveryPlayersWordOnTheRules) {
    using T = net::GameEventType;
    const auto custom = static_cast<std::uint16_t>(T::Custom);
    for (const auto type : {T::CheckpointReached, T::LapCompleted, T::RaceFinished, T::GoldPickedUp,
                            T::GoldDropped, T::GoldDelivered}) {
        EXPECT_FALSE(hostAcceptsGameEvent(1, static_cast<std::uint16_t>(type)));
        EXPECT_TRUE(hostAcceptsGameEvent(net::kHostPlayerId, static_cast<std::uint16_t>(type)));
    }
    for (std::uint16_t t : {std::uint16_t(custom + 1), std::uint16_t(custom + 2), std::uint16_t(custom + 3),
                            net::kRulesEvent})
        EXPECT_FALSE(hostAcceptsGameEvent(3, t));
    EXPECT_TRUE(hostAcceptsGameEvent(1, static_cast<std::uint16_t>(T::LeftRace)));
    EXPECT_TRUE(hostAcceptsGameEvent(1, static_cast<std::uint16_t>(T::Collision)));
    EXPECT_TRUE(hostAcceptsGameEvent(1, net::kVehicleDamageEvent));
}

namespace {

struct RulesRetail {
    city::CityData sf;
    Strings strings;
};

RulesRetail* rulesRetail() {
    static std::unique_ptr<RulesRetail> r = []() -> std::unique_ptr<RulesRetail> {
        if (!test::gameData())
            return nullptr;
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        auto city = city::loadCity(*test::gameData(), "sf");
        if (!src || !city)
            return nullptr;
        auto out = std::make_unique<RulesRetail>();
        out->sf = std::move(*city);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> netSession(GameMode mode, bool host, int laps = 2) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "sf";
    cfg.raceIndex = 0;
    cfg.laps = laps;
    cfg.multiplayer = true;
    SessionOptions o;
    o.seed = 7;
    o.netHost = host;
    o.netRules = true;
    o.playerName = host ? "Host" : "Client";
    std::string error;
    auto s = Session::create(cfg, rulesRetail()->sf, *test::gameData(), rulesRetail()->strings, &error, o);
    EXPECT_TRUE(s) << error;
    if (s) {
        s->start();
        s->setNetStart(0.0f);
    }
    return s;
}

// A player's car at waypoint `index` of the session's race, facing its way.
PlayerState atWaypoint(const Session& s, int index) {
    PlayerState p;
    p.transform = spawnAt(s.checkpoints()[static_cast<std::size_t>(index)]);
    return p;
}

PlayerState away(const Session& s) {
    PlayerState p;
    p.transform = s.playerSpawn();
    p.transform.m3 = p.transform.m3 + Vec3{0.0f, 0.0f, 3000.0f};
    return p;
}

int count(const std::vector<Event>& events, EventType type) {
    return static_cast<int>(std::ranges::count_if(events, [type](const Event& e) { return e.type == type; }));
}

int sounds(const std::vector<Event>& events, GameSound sound) {
    return static_cast<int>(std::ranges::count_if(events, [sound](const Event& e) {
        return e.type == EventType::Sound && e.index == static_cast<int>(sound) && e.value >= 0.0f;
    }));
}

} // namespace

TEST(NetRaceRules, AClientShowsAPredictedCheckpointOnceAndTakesBackOneTheHostNeverCounted) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(rulesRetail());
    auto s = netSession(GameMode::Checkpoint, false);
    ASSERT_TRUE(s);
    ASSERT_GE(s->checkpoints().size(), 4u);
    for (int i = 0; i < 3; ++i)
        s->update(kStep, away(*s));
    ASSERT_EQ(s->phase(), Phase::Racing);
    s->takeEvents();
    // Predicted at sample 100: shown at once, with its sound.
    s->setNetSample(100);
    s->update(kStep, atWaypoint(*s, 1));
    auto ev = s->takeEvents();
    EXPECT_EQ(count(ev, EventType::CheckpointCleared), 1);
    EXPECT_EQ(sounds(ev, GameSound::Waypoint), 1);
    EXPECT_EQ(s->checkpointsCleared(), 1);
    // The host's word confirms it: nothing more.
    s->applyNetProgress({103, 0, {1}}, s->checkpoints()[1].position);
    ev = s->takeEvents();
    EXPECT_TRUE(ev.empty());
    EXPECT_EQ(s->netHits(), (std::vector<std::uint8_t>{1}));
    // Predicted at sample 200, the host's word without it: kept while the
    // host has not run kNetHitMargin samples past it, then taken back
    // without a sound.
    s->setNetSample(200);
    s->update(kStep, atWaypoint(*s, 2));
    s->takeEvents();
    EXPECT_EQ(s->checkpointsCleared(), 2);
    s->applyNetProgress({200 + Session::kNetHitMargin - 1, 0, {1}}, {});
    EXPECT_TRUE(s->takeEvents().empty());
    EXPECT_EQ(s->checkpointsCleared(), 2);
    s->applyNetProgress({200 + Session::kNetHitMargin, 0, {1}}, {});
    ev = s->takeEvents();
    EXPECT_EQ(count(ev, EventType::CheckpointTakenBack), 1);
    EXPECT_EQ(count(ev, EventType::CheckpointCleared), 0);
    EXPECT_EQ(count(ev, EventType::Sound), 0);
    EXPECT_EQ(s->checkpointsCleared(), 1);
    EXPECT_TRUE(s->checkpointVisible(2));
    // One the host counted that this machine did not see: shown when the
    // word comes, once.
    s->applyNetProgress({300, 0, {1, 3}}, {});
    ev = s->takeEvents();
    ASSERT_EQ(count(ev, EventType::CheckpointCleared), 1);
    const auto shown =
        std::ranges::find_if(ev, [](const Event& e) { return e.type == EventType::CheckpointCleared; });
    EXPECT_EQ(shown->index, 3);
    EXPECT_EQ(shown->value, 1.0f); // brought by the host's word
    EXPECT_EQ(sounds(ev, GameSound::Waypoint), 1);
    s->applyNetProgress({330, 0, {1, 3}}, {});
    EXPECT_TRUE(s->takeEvents().empty());
    EXPECT_EQ(s->netHits(), (std::vector<std::uint8_t>{1, 3}));
}

TEST(NetRaceRules, AClientsFinishAndStandingsAreTheHosts) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(rulesRetail());
    auto s = netSession(GameMode::Checkpoint, false);
    ASSERT_TRUE(s);
    const int n = static_cast<int>(s->checkpoints().size());
    for (int i = 0; i < 3; ++i)
        s->update(kStep, away(*s));
    // Every waypoint and the finish predicted: no finish without the host.
    std::uint32_t seq = 10;
    for (int i = 1; i < n; ++i) {
        s->setNetSample(seq += 10);
        s->update(kStep, atWaypoint(*s, i));
    }
    s->takeEvents();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_EQ(s->playerHold(), PlayerHold::None);
    // The host's standings.
    s->setNetStanding(2, 3);
    EXPECT_EQ(s->position(), 2);
    EXPECT_EQ(s->racerCount(), 3);
    // Another player's finish, then this one's with the host's time.
    s->remoteFinished("Other", 80.0f);
    s->netFinished(83.25f);
    auto ev = s->takeEvents();
    EXPECT_EQ(count(ev, EventType::NetFinished), 0); // nothing to claim
    EXPECT_EQ(count(ev, EventType::PlayerFinished), 1);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(s->message2().text.find("1:23:25") != std::string::npos, true);
    // The race waits for the host's 0x211.
    for (int i = 0; i < 6 * 60; ++i)
        s->update(kStep, away(*s));
    EXPECT_EQ(s->phase(), Phase::PostRace);
    s->netAllCounted();
    s->update(kStep, away(*s));
    EXPECT_EQ(s->phase(), Phase::Done);
    const auto r = s->result();
    ASSERT_EQ(r.standings.size(), 2u);
    EXPECT_EQ(r.standings[1].name, "Client");
    EXPECT_FLOAT_EQ(r.standings[1].timeSeconds, 83.25f);
    EXPECT_EQ(r.position, 2);
}

TEST(NetRaceRules, AClientStillRacingAtTheHostsTimeoutDoesNotFinish) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(rulesRetail());
    auto s = netSession(GameMode::Circuit, false);
    ASSERT_TRUE(s);
    for (int i = 0; i < 3; ++i)
        s->update(kStep, away(*s));
    s->remoteFinished("Other", 300.0f);
    // No timeout of its own: only the host's word ends the race.
    for (int i = 0; i < 130 * 60 / 10; ++i)
        s->update(10.0f * kStep, away(*s));
    EXPECT_EQ(s->phase(), Phase::Racing);
    s->takeEvents();
    s->netTimedOut();
    s->netFinished(Session::kNetDnf);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(s->playerHold(), PlayerHold::Undrivable);
    EXPECT_FALSE(s->result().finished);
}

TEST(NetRaceRules, TheHostsMessageReachesAClientWhateverIsLostOrForged) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(rulesRetail());
    auto hs = netSession(GameMode::Checkpoint, true);
    auto cs = netSession(GameMode::Checkpoint, false);
    ASSERT_TRUE(hs && cs);
    NetRules host, client;
    host.begin({true, 0, 7, hs.get(), nullptr, {0, 1}});
    client.begin({false, 1, 7, cs.get(), nullptr, {0, 1}});
    host.setName(0, "Host");
    host.setName(1, "Client");
    client.setName(0, "Host");
    client.setName(1, "Client");
    std::vector<std::vector<std::byte>> wire;
    auto send = [&](std::uint8_t to, std::vector<std::byte> payload) {
        EXPECT_EQ(to, 1);
        wire.push_back(std::move(payload));
    };
    auto event = [](std::vector<std::byte> payload, std::uint8_t from = net::kHostPlayerId) {
        NetGameEvent e;
        e.from = from;
        e.type = static_cast<net::GameEventType>(net::kRulesEvent);
        e.payload = std::move(payload);
        return e;
    };
    for (int i = 0; i < 3; ++i) {
        hs->update(kStep, away(*hs));
        cs->update(kStep, away(*cs));
    }
    // The client's car (on the host) crosses waypoint 1 at its sample 40;
    // the client never predicted it.
    const auto& cps = hs->checkpoints();
    std::uint32_t seq = 0;
    auto drive = [&](int waypoint, int samples) {
        for (int i = 0; i < samples; ++i) {
            const Mat34 at = i == samples / 2 ? spawnAt(cps[static_cast<std::size_t>(waypoint)])
                                              : away(*hs).transform;
            host.hostSample(1, ++seq, at, kBox, false);
            host.hostSample(0, seq, away(*hs).transform, kBox, false);
        }
    };
    drive(1, 80);
    host.hostUpdate(kStep, 1000, away(*hs).transform.m3, send);
    ASSERT_EQ(wire.size(), 1u);
    // A forged message (from another player), a malformed one and one for
    // another race are refused; then the host's is taken.
    net::RulesMsg forged;
    forged.race = 7;
    forged.seq = 50;
    forged.hits = {1, 2, 3};
    client.receive({event(net::encodePayload(forged), 2)}, {});
    std::vector<std::byte> junk(wire[0].begin(), wire[0].begin() + 5);
    client.receive({event(junk)}, {});
    forged.race = 6;
    client.receive({event(net::encodePayload(forged))}, {});
    EXPECT_EQ(client.stats().refused, 1u);
    EXPECT_EQ(client.stats().malformed, 1u);
    EXPECT_EQ(client.stats().stale, 1u);
    EXPECT_TRUE(cs->netHits().empty());
    client.receive({event(wire[0])}, {});
    EXPECT_EQ(cs->netHits(), (std::vector<std::uint8_t>{1}));
    // The same message again (a duplicate) changes nothing.
    client.receive({event(wire[0])}, {});
    EXPECT_EQ(client.stats().stale, 2u);
    // The client finishes on the host; its message is lost; the next one's
    // results stand in for the decision.
    for (int w = 2; w < static_cast<int>(cps.size()); ++w)
        drive(w, 40);
    wire.clear();
    host.hostUpdate(kStep, 2000, away(*hs).transform.m3, send);
    ASSERT_EQ(wire.size(), 1u);
    ASSERT_FALSE(host.decisions().empty());
    EXPECT_EQ(host.decisions().back().type, net::RuleDecision::Finished);
    wire.clear(); // lost (ENet would resend it; the state does not need it)
    host.hostUpdate(kStep, 2000 + NetRules::kStateIntervalMs, away(*hs).transform.m3, send);
    ASSERT_EQ(wire.size(), 1u);
    client.receive({event(wire[0])}, {});
    EXPECT_TRUE(cs->netFinishKnown());
    EXPECT_EQ(cs->phase(), Phase::PostRace);
    EXPECT_TRUE(client.finished(1));
    // The host's own session has the client's finish in its results.
    EXPECT_TRUE(host.finished(1));
}

// --- Through live sessions ------------------------------------------------------------------

namespace {

// This file's block of ports (below 49152; see test_netgame.cpp).
std::uint16_t rulesPort() {
    static const std::uint16_t port = static_cast<std::uint16_t>(
        25000 + (std::chrono::steady_clock::now().time_since_epoch().count() / 1000) % 1000 * 4);
    return port;
}

NetOptions rulesOptions(const std::string& name) {
    NetOptions o;
    o.playerName = name;
    o.port = rulesPort();
    o.portMapping = false;
    o.discoveryPort = static_cast<std::uint16_t>(rulesPort() + 1);
    return o;
}

bool pumpAll(std::initializer_list<NetGame*> games, const std::function<bool()>& done, int timeoutMs = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        for (NetGame* g : games)
            g->update();
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST(NetRaceRules, AClientTakesTheNewestStateOfEitherKind) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(rulesRetail());
    // Protocol 14: the race's state also goes unreliably, 20 times a second
    // and at once on a hit; whichever copy arrives first stands, and the
    // reliable messages' decisions are taken whatever their state's age.
    auto hs = netSession(GameMode::Checkpoint, true);
    auto cs = netSession(GameMode::Checkpoint, false);
    ASSERT_TRUE(hs && cs);
    NetRules host, client;
    host.begin({true, 0, 7, hs.get(), nullptr, {0, 1}});
    client.begin({false, 1, 7, cs.get(), nullptr, {0, 1}});
    for (auto* r : {&host, &client}) {
        r->setName(0, "Host");
        r->setName(1, "Client");
    }
    std::vector<std::vector<std::byte>> wire;
    std::vector<net::RulesMsg> states;
    auto send = [&](std::uint8_t, std::vector<std::byte> payload) { wire.push_back(std::move(payload)); };
    auto sendState = [&](std::uint8_t to, const net::RulesStateMsg& st) {
        EXPECT_EQ(to, 1);
        // What the State channel carries: encoded and decoded again.
        net::RulesStateMsg back;
        EXPECT_TRUE(net::decodeMessage(net::encodeMessage(st), back));
        states.push_back(back.rules);
    };
    auto event = [](const std::vector<std::byte>& payload) {
        NetGameEvent e;
        e.from = net::kHostPlayerId;
        e.type = static_cast<net::GameEventType>(net::kRulesEvent);
        e.payload = payload;
        return e;
    };
    for (int i = 0; i < 3; ++i) {
        hs->update(kStep, away(*hs));
        cs->update(kStep, away(*cs));
    }
    const auto& cps = hs->checkpoints();
    std::uint32_t seq = 0;
    auto drive = [&](int waypoint, int samples) {
        for (int i = 0; i < samples; ++i) {
            const Mat34 at = i == samples / 2 ? spawnAt(cps[static_cast<std::size_t>(waypoint)])
                                              : away(*hs).transform;
            host.hostSample(1, ++seq, at, kBox, false);
            host.hostSample(0, seq, away(*hs).transform, kBox, false);
        }
    };
    // Nothing happened: the first message goes both ways, then only the
    // state, every 50 ms.
    host.hostUpdate(kStep, 1000, away(*hs).transform.m3, send, sendState);
    EXPECT_EQ(wire.size(), 1u);
    EXPECT_EQ(states.size(), 1u);
    host.hostUpdate(kStep, 1030, away(*hs).transform.m3, send, sendState);
    EXPECT_EQ(states.size(), 1u);
    host.hostUpdate(kStep, 1050, away(*hs).transform.m3, send, sendState);
    EXPECT_EQ(wire.size(), 1u);
    ASSERT_EQ(states.size(), 2u);
    EXPECT_TRUE(states[1].decisions.empty());
    EXPECT_TRUE(states[1].results.empty());
    EXPECT_GT(states[1].seq, states[0].seq);
    wire.clear();
    states.clear();
    // A hit: both at once. The state arrives first and is taken; the
    // reliable message after it is older (its state left alone).
    drive(1, 80);
    host.hostUpdate(kStep, 1060, away(*hs).transform.m3, send, sendState);
    ASSERT_EQ(wire.size(), 1u);
    ASSERT_EQ(states.size(), 1u);
    client.receiveStates(states, {});
    EXPECT_EQ(cs->netHits(), (std::vector<std::uint8_t>{1}));
    EXPECT_EQ(client.stats().statesTaken, 1u);
    client.receive({event(wire[0])}, {});
    EXPECT_EQ(cs->netHits(), (std::vector<std::uint8_t>{1}));
    EXPECT_EQ(client.stats().received, 1u);
    // The same state again, another race's, one with decisions: refused.
    client.receiveStates(states, {});
    net::RulesMsg other = states[0];
    other.seq = 900;
    other.race = 6;
    client.receiveStates({other}, {});
    other.race = 7;
    other.decisions.push_back({901, net::RuleDecision::TimedOut});
    client.receiveStates({other}, {});
    EXPECT_EQ(client.stats().statesTaken, 1u);
    EXPECT_FALSE(cs->netFinishKnown());
    wire.clear();
    states.clear();
    // The client's car finishes: the decision is reliable only; the state
    // that comes first does not finish it, the older reliable message does,
    // once.
    for (int w = 2; w < static_cast<int>(cps.size()); ++w)
        drive(w, 40);
    host.hostUpdate(kStep, 1100, away(*hs).transform.m3, send, sendState);
    ASSERT_EQ(wire.size(), 1u);
    ASSERT_EQ(states.size(), 1u);
    client.receiveStates(states, {});
    EXPECT_FALSE(cs->netFinishKnown());
    client.receive({event(wire[0])}, {});
    EXPECT_TRUE(cs->netFinishKnown());
    EXPECT_TRUE(client.finished(1));
    const auto decisions = client.stats().decisions;
    client.receive({event(wire[0])}, {}); // a duplicate: stale
    EXPECT_EQ(client.stats().decisions, decisions);
}

TEST(NetRaceRules, APlayersOwnWordOnTheRulesReachesNobody) {
    NetGame host(rulesOptions("Hosty")), cheat(rulesOptions("Cheaty")), honest(rulesOptions("Honesty"));
    RaceConfig race;
    race.mode = GameMode::Checkpoint;
    race.city = "sf";
    race.raceIndex = 0;
    NetHostOptions ho;
    ho.advertiseOnLan = false;
    std::string err;
    ASSERT_TRUE(host.host(race, ho, {"vpbug", 0, 0}, &err)) << err;
    const std::string at = std::format("127.0.0.1:{}", rulesPort());
    ASSERT_TRUE(cheat.join(at, "", {"vpcab", 0, 0}, &err)) << err;
    ASSERT_TRUE(honest.join(at, "", {"vpcab", 0, 0}, &err)) << err;
    ASSERT_TRUE(pumpAll({&host, &cheat, &honest}, [&] {
        return host.players().size() == 3 && cheat.phase() == NetGame::Phase::Lobby &&
               honest.phase() == NetGame::Phase::Lobby && honest.players().size() == 3;
    }));
    cheat.setReady(true);
    honest.setReady(true);
    ASSERT_TRUE(pumpAll({&host, &cheat, &honest}, [&] { return host.everyoneReady(false); }));
    host.startRace();
    ASSERT_TRUE(pumpAll({&host, &cheat, &honest}, [&] {
        return host.phase() == NetGame::Phase::Countdown && cheat.phase() == NetGame::Phase::Countdown &&
               honest.phase() == NetGame::Phase::Countdown;
    }));
    // The cheat claims checkpoints, a finish, the gold, Cops and Robbers'
    // places and limit and a rules message of its own, to everyone and to
    // one player; then a collision, which is no claim.
    cheat.sendCheckpoint(9, 1000);
    cheat.sendLap(3, 1000);
    cheat.sendFinish(1000, 1);
    cheat.sendGold(net::GameEventType::GoldPickedUp, {}, 0);
    cheat.sendGold(net::GameEventType::GoldDelivered, {}, 0);
    const auto custom = static_cast<std::uint16_t>(net::GameEventType::Custom);
    for (std::uint16_t t : {std::uint16_t(custom + 1), std::uint16_t(custom + 2), std::uint16_t(custom + 3)})
        cheat.sendEvent(t, net::encodePayload(net::GoldEvent{}));
    net::RulesMsg forged;
    forged.race = host.raceNumber();
    forged.seq = 1;
    forged.hits = {1, 2, 3};
    cheat.sendEvent(net::kRulesEvent, net::encodePayload(forged));
    cheat.sendEvent(net::kRulesEvent, net::encodePayload(forged), honest.localId());
    cheat.sendCollision(net::kHostPlayerId, {}, 5.0f);
    std::vector<NetGameEvent> atHost, atHonest;
    ASSERT_TRUE(pumpAll({&host, &cheat, &honest}, [&] {
        for (auto& e : host.takeGameEvents())
            atHost.push_back(std::move(e));
        for (auto& e : honest.takeGameEvents())
            atHonest.push_back(std::move(e));
        return !atHost.empty() && !atHonest.empty();
    }));
    pumpAll({&host, &cheat, &honest}, [] { return false; }, 200);
    for (auto& e : host.takeGameEvents())
        atHost.push_back(std::move(e));
    for (auto& e : honest.takeGameEvents())
        atHonest.push_back(std::move(e));
    ASSERT_EQ(atHost.size(), 1u);
    EXPECT_EQ(atHost[0].type, net::GameEventType::Collision);
    ASSERT_EQ(atHonest.size(), 1u);
    EXPECT_EQ(atHonest[0].type, net::GameEventType::Collision);
    // The host's own rules message reaches its player, from the host.
    net::RulesMsg word;
    word.race = host.raceNumber();
    word.seq = 1;
    host.sendEvent(net::kRulesEvent, net::encodePayload(word), honest.localId());
    std::vector<NetGameEvent> got;
    ASSERT_TRUE(pumpAll({&host, &honest}, [&] {
        for (auto& e : honest.takeGameEvents())
            got.push_back(std::move(e));
        return !got.empty();
    }));
    EXPECT_EQ(static_cast<std::uint16_t>(got[0].type), net::kRulesEvent);
    EXPECT_EQ(got[0].from, net::kHostPlayerId);
}
