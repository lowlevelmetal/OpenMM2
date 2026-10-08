// Parity checks of the game modes' spawns and rules against MM2's own code
// (MM2Recomp, build 3393): the modes' InitGameObjects / InitOtherPlayers,
// mmGame::CollideAIOpponents, mmGame::FindGroundPos and the water handlers.
// See docs/parity/mm2/game-flow.md.

#include "TestData.h"
#include "city/CityData.h"
#include "game/Strings.h"
#include "game/session/CopsAndRobbers.h"
#include "game/session/RaceSetup.h"
#include "game/session/Session.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

// A probe of the plane y = `height` (the level as dgPhysManager::Collide sees
// it, for the tests).
GroundProbe planeProbe(float height) {
    return [height](const Vec3& from, const Vec3& to) -> std::optional<Vec3> {
        phys::FlatGround ground(height);
        phys::RayHit hit;
        if (!ground.probe(from, to, hit))
            return std::nullopt;
        return hit.position;
    };
}

struct FlowRetail {
    city::CityData london;
    Strings strings;
};

FlowRetail* flowRetail() {
    static std::unique_ptr<FlowRetail> r = []() -> std::unique_ptr<FlowRetail> {
        if (!test::gameData())
            return nullptr;
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        auto london = city::loadCity(*test::gameData(), "london");
        if (!src || !london)
            return nullptr;
        auto out = std::make_unique<FlowRetail>();
        out->london = std::move(*london);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> flowSession(GameMode mode, int index, bool multiplayer = false) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "london";
    cfg.raceIndex = index;
    cfg.multiplayer = multiplayer;
    SessionOptions options;
    options.seed = 11;
    std::string error;
    auto s = Session::create(cfg, flowRetail()->london, *test::gameData(), flowRetail()->strings, &error, options);
    EXPECT_TRUE(s) << error;
    return s;
}

} // namespace

// mmWaypoints::GetStart / GetStartAngle as the modes' InitGameObjects use
// them: the waypoint itself, its heading x -0.017453292.
TEST(GameFlowParity, StartPlaceIsTheWaypointAndItsNegatedHeading) {
    Checkpoint cp;
    cp.position = {10.0f, 2.0f, -30.0f};
    cp.headingDeg = 90.0f;
    const ResetPlace p = startPlace(cp);
    EXPECT_EQ(p.position.x, 10.0f);
    EXPECT_EQ(p.position.y, 2.0f);
    EXPECT_EQ(p.position.z, -30.0f);
    EXPECT_FLOAT_EQ(p.angle, 90.0f * -0.017453292f);
    // The same orientation as spawnAt's.
    const Mat34 r = Mat34::rotationY(p.angle);
    const Mat34 s = spawnAt(cp);
    EXPECT_NEAR(r.m2.x, s.m2.x, 1e-6f);
    EXPECT_NEAR(r.m2.z, s.m2.z, 1e-6f);
}

// mmGame::InitOtherPlayers / CollideAIOpponents: from 2 m above the body to
// 10 m below it; the reset place goes 0.9 m above the hit.
TEST(GameFlowParity, SettleOnGroundProbesFromTheBodyAndAddsNinetyCentimetres) {
    const auto settled = settleOnGround({5.0f, 3.0f, 7.0f}, planeProbe(1.0f));
    ASSERT_TRUE(settled);
    EXPECT_FLOAT_EQ(settled->x, 5.0f);
    EXPECT_FLOAT_EQ(settled->y, 1.9f);
    EXPECT_FLOAT_EQ(settled->z, 7.0f);
    // Ground 2 m above the body is still found (the probe starts there) ...
    EXPECT_TRUE(settleOnGround({0.0f, 0.0f, 0.0f}, planeProbe(1.99f)));
    // ... but not above that, nor more than 10 m below: the car stays.
    EXPECT_FALSE(settleOnGround({0.0f, 0.0f, 0.0f}, planeProbe(2.5f)));
    EXPECT_FALSE(settleOnGround({0.0f, 0.0f, 0.0f}, planeProbe(-10.5f)));
}

// mmGame::FindGroundPos: from 7.5 m above to 15 m below; the hit itself, or
// the point when nothing is hit.
TEST(GameFlowParity, FindGroundPosProbesSevenAndAHalfUpFifteenDown) {
    const Vec3 p{1.0f, 0.0f, 2.0f};
    EXPECT_FLOAT_EQ(findGroundPos(p, planeProbe(7.0f)).y, 7.0f);
    EXPECT_FLOAT_EQ(findGroundPos(p, planeProbe(-14.0f)).y, -14.0f);
    EXPECT_FLOAT_EQ(findGroundPos(p, planeProbe(8.0f)).y, 0.0f);
    EXPECT_FLOAT_EQ(findGroundPos(p, planeProbe(-16.0f)).y, 0.0f);
}

// A race start as MM2 makes it: the body at the start + CenterOfGravity, then
// settled: the body ends 0.9 m + CG above the road, so the model origin (at
// the bottom of MM2's car models) is 0.9 + 2 CG.y above it and the car drops
// onto its wheels.
TEST(GameFlowParity, RaceStartLeavesTheCarAboveTheRoad) {
    phys::CarSimParams params;
    params.centerOfGravity = {0.0f, -0.1f, 0.15f};
    phys::CarSim car;
    car.init(params, phys::VehicleGeometry::placeholder());
    const ResetPlace start{{0.0f, 0.0f, 0.0f}, 0.0f};
    car.resetAt(start.position, start.angle);
    const auto settled = settleOnGround(car.body.ics.matrix.m3, planeProbe(0.0f));
    ASSERT_TRUE(settled);
    car.resetAt(*settled, start.angle);
    EXPECT_NEAR(car.body.ics.matrix.m3.y, 0.8f, 1e-6f);
    EXPECT_NEAR(car.modelMatrix().m3.y, 0.7f, 1e-6f);
}

// Where each mode puts the player (retail data): the race modes on the first
// waypoint with its angle, settled on the ground; cruise 2 m above a random
// intersection, facing -Z, as it is (mmSingleRoam::InitOtherPlayers).
TEST(GameFlowParity, PlayerPlacePerMode) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    for (const GameMode mode : {GameMode::Blitz, GameMode::Circuit, GameMode::Checkpoint}) {
        auto s = flowSession(mode, 0);
        ASSERT_TRUE(s);
        const auto& setup = s->setup();
        ASSERT_FALSE(setup.checkpoints.empty());
        EXPECT_EQ(setup.playerDrop, StartDrop::OnGround);
        EXPECT_EQ(setup.playerPlace.position.y, setup.checkpoints.front().position.y);
        EXPECT_FLOAT_EQ(setup.playerPlace.angle, setup.checkpoints.front().headingDeg * -0.017453292f);
    }
    auto cruise = flowSession(GameMode::Cruise, -1);
    ASSERT_TRUE(cruise);
    EXPECT_EQ(cruise->setup().playerDrop, StartDrop::None);
    EXPECT_EQ(cruise->setup().playerPlace.angle, 0.0f);
    bool atIntersection = false;
    for (const auto& x : flowRetail()->london.aiMap->intersections)
        if (x.center.x == cruise->setup().playerPlace.position.x && x.center.z == cruise->setup().playerPlace.position.z &&
            x.center.y + 2.0f == cruise->setup().playerPlace.position.y)
            atIntersection = true;
    EXPECT_TRUE(atIntersection);
}

// mmSingleCircuit::HitWaterHandler: the car is reset at the last waypoint
// cleared with that waypoint's angle (and no ground probe); the start stays
// the reset place for a restart.
TEST(GameFlowParity, CircuitWaterRespawnsAtTheLastCheckpointPlace) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = flowSession(GameMode::Circuit, 0);
    ASSERT_TRUE(s);
    s->start();
    EXPECT_FALSE(s->respawnPlace());
    PlayerState p;
    p.transform = s->playerSpawn();
    p.inWater = true;
    std::vector<Event> events;
    for (int i = 0; i < 400 && !s->respawnPlace(); ++i) {
        s->update(1.0f / 60.0f, p);
        for (auto& e : s->takeEvents())
            events.push_back(e);
    }
    ASSERT_TRUE(s->respawnPlace());
    const ResetPlace expected = startPlace(s->checkpoints().front());
    EXPECT_EQ(s->respawnPlace()->position.y, expected.position.y);
    EXPECT_FLOAT_EQ(s->respawnPlace()->angle, expected.angle);
}

namespace {

// Runs a session from "Go!" with the player parked on its start.
struct FlowRun {
    Session& s;
    PlayerState player;
    std::vector<OpponentState> opponents;
    explicit FlowRun(Session& session) : s(session) {
        player.transform = s.playerSpawn();
        opponents.resize(s.opponents().size());
        for (std::size_t i = 0; i < opponents.size(); ++i)
            opponents[i].transform = s.opponents()[i].spawn;
        s.start();
        for (int i = 0; i < 900 && s.phase() == Phase::Countdown; ++i)
            tick();
    }
    void tick() {
        s.update(1.0f / 30.0f, player, opponents);
        s.takeEvents();
    }
    void untilOver(int maxTicks = 3000) {
        for (int i = 0; i < maxTicks && s.phase() == Phase::Racing; ++i)
            tick();
    }
};

} // namespace

// mmSingleBlitz::UpdateGame's wreck: the post-race camera, the music ended on
// the beat (StopSegment(1)) and the finish stand hidden (DeactivateFinish).
TEST(GameFlowParity, BlitzWreckTurnsToThePostRaceCameraAndHidesTheFinish) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = flowSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    FlowRun run(*s);
    ASSERT_EQ(s->phase(), Phase::Racing);
    const std::size_t last = s->checkpoints().size() - 1;
    EXPECT_TRUE(s->checkpointVisible(last));
    run.player.wrecked = true;
    run.tick();
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_TRUE(s->postRaceCamera());
    EXPECT_TRUE(s->damagedOut());
    EXPECT_FALSE(s->musicStopped());
    EXPECT_FALSE(s->checkpointVisible(last));
    EXPECT_FALSE(s->raceOver());
}

// mmSingleBlitz / mmSingleRace::HitWaterHandler: the race is lost but the
// camera and the music stay as they are.
TEST(GameFlowParity, WaterLossKeepsTheCameraAndTheMusic) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    for (const GameMode mode : {GameMode::Blitz, GameMode::Checkpoint}) {
        auto s = flowSession(mode, 0);
        ASSERT_TRUE(s);
        FlowRun run(*s);
        run.player.inWater = true;
        run.untilOver(400);
        EXPECT_EQ(s->phase(), Phase::PostRace);
        EXPECT_FALSE(s->postRaceCamera());
        EXPECT_FALSE(s->damagedOut());
        EXPECT_FALSE(s->musicStopped());
        EXPECT_FALSE(s->raceOver());
    }
}

// mmGameSingle::DisableRacers / EnableRacers: no player damage before "Go!";
// UpdateJump never calls EnableRacers, so a jump lesson has none at all.
TEST(GameFlowParity, PlayerDamageIsOffUntilGoAndInJumpLessons) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto blitz = flowSession(GameMode::Blitz, 0);
    ASSERT_TRUE(blitz);
    EXPECT_FALSE(blitz->playerDamageEnabled());
    FlowRun run(*blitz);
    ASSERT_EQ(blitz->phase(), Phase::Racing);
    EXPECT_TRUE(blitz->playerDamageEnabled());
    blitz->restart();
    EXPECT_FALSE(blitz->playerDamageEnabled());

    auto jump = flowSession(GameMode::CrashCourse, 0);
    ASSERT_TRUE(jump);
    FlowRun jumpRun(*jump);
    ASSERT_EQ(jump->phase(), Phase::Racing);
    EXPECT_FALSE(jump->playerDamageEnabled());

    auto cruise = flowSession(GameMode::Cruise, -1);
    ASSERT_TRUE(cruise);
    EXPECT_TRUE(cruise->playerDamageEnabled());
}

// mmGameMulti::Init: no racers nor police in multiplayer; the race modes
// load no AI map at all.
TEST(GameFlowParity, MultiplayerHasNoRacersOrPolice) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    for (const GameMode mode : {GameMode::Checkpoint, GameMode::Circuit, GameMode::Cruise}) {
        auto s = flowSession(mode, mode == GameMode::Cruise ? -1 : 0, true);
        ASSERT_TRUE(s);
        EXPECT_TRUE(s->opponents().empty());
        EXPECT_TRUE(s->police().empty());
    }
}

// mmGameMulti::UpdateScore, mmMultiRace::GameMessage / SetTimeoutOn and
// UpdateResults: the place among the other players, their finish line, the
// 60 s finish timeout from the first finish ("Race over" and a did-not-
// finish), and the results by time with the losers last.
TEST(GameFlowParity, MultiplayerRaceStandingsTimeoutAndResults) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = flowSession(GameMode::Checkpoint, 0, true);
    ASSERT_TRUE(s);
    s->setStartSignal(true);
    FlowRun run(*s);
    ASSERT_EQ(s->phase(), Phase::Racing);
    Session::NetRacer ahead;
    ahead.name = "Ahead";
    ahead.waypoints = 3;
    ahead.position = s->playerSpawn().m3;
    Session::NetRacer behind = ahead;
    behind.name = "Behind";
    behind.waypoints = 1;
    behind.position = s->playerSpawn().m3 + Vec3{0.0f, 0.0f, 500.0f};
    s->setNetRacers({ahead, behind});
    run.tick();
    EXPECT_EQ(s->position(), 2);
    EXPECT_EQ(s->racerCount(), 3);

    // The leader finishes: its line, and the 60 s timeout starts.
    s->remoteFinished("Ahead", 95.0f);
    EXPECT_EQ(s->message().text, "Ahead");
    ahead.finished = true;
    s->setNetRacers({ahead, behind});
    std::vector<Event> events;
    for (int i = 0; i < 59 * 30; ++i) {
        s->update(1.0f / 30.0f, run.player, run.opponents);
        for (const auto& e : s->takeEvents())
            events.push_back(e);
    }
    EXPECT_EQ(s->phase(), Phase::Racing);
    for (int i = 0; i < 2 * 30 && s->phase() == Phase::Racing; ++i) {
        s->update(1.0f / 30.0f, run.player, run.opponents);
        for (const auto& e : s->takeEvents())
            events.push_back(e);
    }
    ASSERT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(s->playerHold(), PlayerHold::Undrivable);
    EXPECT_TRUE(std::ranges::any_of(events, [](const Event& e) {
        return e.type == EventType::NetFinished && e.value >= Session::kNetDnf;
    }));
    // Timed out: the results follow without waiting for the third player.
    for (int i = 0; i < 4 * 30 && s->phase() != Phase::Done; ++i)
        run.tick();
    EXPECT_EQ(s->phase(), Phase::Done);
    const auto r = s->result();
    ASSERT_EQ(r.standings.size(), 2u);
    EXPECT_EQ(r.standings[0].name, "Ahead");
    EXPECT_FALSE(r.standings[0].dnf);
    EXPECT_EQ(r.standings[1].opponent, -1);
    EXPECT_TRUE(r.standings[1].dnf);
    EXPECT_EQ(r.position, 0);
}

// mmMultiCR::DropGold / FindGround, ImpactCallback, UpdateGold and
// SystemMessage: the dropped gold on the ground (back at its place only from
// deep water or a forced drop), "You dropped the gold!" only for a hit, no
// pickup by a host alone, and a leaver's gold dropped by the host.
TEST(GameFlowParity, CopsAndRobbersGoldRules) {
    CrLocations loc;
    for (int i = 0; i < 6; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    CrSettings settings;
    settings.mode = CopsAndRobbersMode::FreeForAll;
    settings.seed = 7;
    settings.findGround = [](const Vec3& p) { return Vec3{p.x, -1.0f, p.z}; };
    settings.canDropAt = [](const Vec3& p) { return p.x < 1000.0f; }; // "deep water" beyond x = 1000
    CopsAndRobbers host(settings, loc);
    host.addCar(0, CrTeam::Robber);
    host.addCar(1, CrTeam::Robber);
    using Type = CopsAndRobbers::Message::Type;
    using Car = CopsAndRobbers::Car;

    // The host alone at the gold does not take it.
    std::vector<Car> alone{{0, CrTeam::Robber, host.set().gold, false, false}};
    EXPECT_TRUE(host.updateNetwork(0.1f, 0, true, alone, {}).empty());
    EXPECT_EQ(host.goldCarrier(), -1);
    // With another player in the game it does.
    std::vector<Car> cars{{0, CrTeam::Robber, host.set().gold, false, false},
                          {1, CrTeam::Robber, {5000, 0, 5000}, false, false}};
    auto sent = host.updateNetwork(0.1f, 0, true, cars, {});
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent[0].type, Type::GoldTaken);
    host.takeEvents();

    // A hit knocks it loose where the car is, on the ground.
    cars[0].position = {300.0f, 4.0f, 20.0f};
    sent = host.updateNetwork(0.1f, 0, true, cars, {{0, 1, 300.0f}});
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent[0].type, Type::GoldDropped);
    EXPECT_EQ(host.goldPosition(), (Vec3{300.0f, -1.0f, 20.0f}));
    auto events = host.takeEvents();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().value, 1); // knocked loose: "You dropped the gold!"

    // Carried by player 1, who leaves: the host drops it where it was.
    host.receive({Type::PickupRequest, 1}, 1, true);
    ASSERT_EQ(host.goldCarrier(), 1);
    cars[1].position = {700.0f, 0.0f, 0.0f};
    host.updateNetwork(0.1f, 0, true, cars, {});
    sent = host.playerLeft(1, true);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].type, Type::GoldDropped);
    EXPECT_EQ(host.goldCarrier(), -1);
    EXPECT_EQ(host.goldPosition(), (Vec3{700.0f, -1.0f, 0.0f}));

    // Dropped in deep water: back at the set's place, on the ground.
    CopsAndRobbers h2(settings, loc);
    h2.addCar(0, CrTeam::Robber);
    h2.addCar(1, CrTeam::Robber);
    std::vector<Car> c2{{0, CrTeam::Robber, h2.set().gold, false, false},
                        {1, CrTeam::Robber, {5000, 0, 5000}, false, false}};
    h2.updateNetwork(0.1f, 0, true, c2, {});
    ASSERT_EQ(h2.goldCarrier(), 0);
    c2[0].position = {2000.0f, 0.0f, 0.0f};
    c2[0].wrecked = true;
    h2.updateNetwork(0.1f, 0, true, c2, {});
    EXPECT_EQ(h2.goldPosition(), (Vec3{h2.set().gold.x, -1.0f, h2.set().gold.z}));
}

// mmSingleStunt::UpdateJump's time-up: no post-race camera, the finish stand
// hidden; the race-over flag is set (Escape then shows the results).
TEST(GameFlowParity, JumpLessonTimeUpHidesTheFinishWithoutTheCamera) {
    MM2_REQUIRE_GAME_DATA();
    if (!flowRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = flowSession(GameMode::CrashCourse, 0);
    ASSERT_TRUE(s);
    ASSERT_TRUE(s->currentLesson());
    ASSERT_EQ(s->currentLesson()->type, LessonType::Jump);
    FlowRun run(*s);
    ASSERT_EQ(s->phase(), Phase::Racing);
    run.untilOver(static_cast<int>((s->currentLesson()->timeLimit + 2.0f) * 30.0f));
    ASSERT_EQ(s->phase(), Phase::PostRace);
    EXPECT_FALSE(s->postRaceCamera());
    EXPECT_FALSE(s->musicStopped());
    EXPECT_FALSE(s->checkpointVisible(s->checkpoints().size() - 1));
    EXPECT_TRUE(s->raceOver());
}
