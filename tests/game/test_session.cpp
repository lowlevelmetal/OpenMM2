#include "TestData.h"
#include "city/CityData.h"
#include "city/RoomLocator.h"
#include "core/StringUtil.h"
#include "game/Strings.h"
#include "game/session/Gate.h"
#include "game/session/Session.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <map>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

// A car facing `forward` (y up) at `pos`.
Mat34 carFacing(const Vec3& forward, const Vec3& pos = {}) {
    Mat34 m;
    m.m2 = -forward;
    m.m1 = Vec3::yAxis();
    m.m0 = m.m1.cross(m.m2).normalized();
    m.m3 = pos;
    return m;
}

} // namespace

// --- Gate geometry (no game data) ---------------------------------------------

TEST(SessionGate, GatePointsAreAcrossTheDrivingDirection) {
    // Heading 0 faces -Z; the gate must lie along X.
    const Checkpoint cp = makeCheckpoint({10, 0, 20}, 0.0f, 5.0f);
    EXPECT_NEAR(cp.gateA.x, 15.0f, 1e-4f);
    EXPECT_NEAR(cp.gateA.y, 20.0f, 1e-4f);
    EXPECT_NEAR(cp.gateB.x, 5.0f, 1e-4f);
    const Vec3 dir = headingDirection(0.0f);
    EXPECT_NEAR(dir.z, -1.0f, 1e-6f);
    // Heading 90 faces +X; spawnAt faces the same way.
    const Mat34 m = spawnAt(makeCheckpoint({}, 90.0f, 5.0f));
    EXPECT_NEAR((-m.m2).x, 1.0f, 1e-5f);
    EXPECT_NEAR(headingDirection(90.0f).x, 1.0f, 1e-5f);
}

TEST(SessionGate, LineIntersectIsSlopeInterceptWithBoxes) {
    // mmWaypointObject::LineIntersect.
    EXPECT_TRUE(lineIntersect({0, -1}, {0, 1}, {-1, 0}, {1, 0}, 0.0f));
    EXPECT_FALSE(lineIntersect({0, 1}, {0, 2}, {-1, 0}, {1, 0}, 0.0f));
    // The tolerance grows the boxes: a segment ending 0.5 m short counts with 1 m.
    EXPECT_TRUE(lineIntersect({0, 0.5f}, {0, 2}, {-1, 0}, {1, 0}, 1.0f));
    // Parallel lines never meet.
    EXPECT_FALSE(lineIntersect({-1, 1}, {1, 1}, {-1, 0}, {1, 0}, 5.0f));
    // A point acts as a vertical line through it: within the tolerance of
    // the gate line it hits.
    EXPECT_TRUE(lineIntersect({0.3f, 0.8f}, {0.3f, 0.8f}, {-1, 0}, {1, 0}, 1.0f));
    EXPECT_FALSE(lineIntersect({0.3f, 1.5f}, {0.3f, 1.5f}, {-1, 0}, {1, 0}, 1.0f));
}

TEST(SessionGate, PlayerTestsNoseToTwoMetresBehindTheTail) {
    // Gate from x=-5 to x=5 at z=0; InertiaBox 2 x 1 x 3 (half length 1.5).
    const Checkpoint cp = makeCheckpoint({0, 0, 0}, 0.0f, 5.0f);
    const Vec3 box{2, 1, 3};
    const Vec3 north{0, 0, -1};
    // Approaching: the nose is 1.5 m ahead and the half width (1 m) of
    // slack grows the segment's box, so the gate counts 2.5 m ahead.
    EXPECT_FALSE(playerGateHit(cp, carFacing(north, {0, 0, 2.6f}), box));
    EXPECT_TRUE(playerGateHit(cp, carFacing(north, {0, 0, 2.4f}), box));
    // Past it, the segment reaches 3.5 m behind the car centre (+1 m).
    EXPECT_TRUE(playerGateHit(cp, carFacing(north, {0, 0, -4.4f}), box));
    EXPECT_FALSE(playerGateHit(cp, carFacing(north, {0, 0, -4.6f}), box));
    // Beside the gate (beyond the 1 m slack).
    EXPECT_FALSE(playerGateHit(cp, carFacing(north, {6.5f, 0, 0}), box));
    EXPECT_TRUE(playerGateHit(cp, carFacing(north, {5.5f, 0, 0}), box));
    // Parked along the gate line: the lateral axis crosses it.
    EXPECT_TRUE(playerGateHit(cp, carFacing({1, 0, 0}, {0, 0, 0.5f}), box));
}

TEST(SessionGate, OpponentsTestAFivefoldBox) {
    // mmWaypoints::AIWPHit: +- one InertiaBox length, the box scaled by 5 as
    // slack (10 m for a 2 m wide car), so a car 12 m short of the gate hits.
    const Checkpoint cp = makeCheckpoint({0, 0, 0}, 0.0f, 5.0f);
    const Vec3 box{2, 1, 3};
    EXPECT_TRUE(aiGateHit(cp, carFacing({0, 0, -1}, {0, 0, 12.0f}), box));
    EXPECT_FALSE(aiGateHit(cp, carFacing({0, 0, -1}, {0, 0, 14.0f}), box));
    EXPECT_TRUE(radiusHit(cp, {0, 0, 4.9f}));
    EXPECT_FALSE(radiusHit(cp, {0, 3, 4.5f}));
}

TEST(SessionGate, WaypointListsAsMmWaypointsLoadsThem) {
    std::vector<city::Waypoint> pts(4);
    pts[0].position = {0, 0, 0};
    pts[0].heading = 0.0f; // the start keeps its heading
    pts[1].position = {0, 0, -100};
    pts[1].heading = 0.0f; // turns towards the next one
    pts[1].radius = 12.9f; // read with atoi
    pts[2].position = {100, 0, -100};
    pts[2].heading = 45.0f; // kept
    pts[2].extra = {"1"};   // hit flag
    pts[3].position = {100, 0, 0};
    pts[3].heading = 0.0f; // the last one has no next waypoint
    auto cps = buildCheckpoints(pts, false);
    ASSERT_EQ(cps.size(), 4u);
    EXPECT_FLOAT_EQ(cps[0].headingDeg, 0.0f);
    EXPECT_FLOAT_EQ(cps[0].radius, 15.0f);
    EXPECT_FLOAT_EQ(cps[1].radius, 12.0f);
    // Heading towards +X from (0, -100): driving direction (1, 0, 0).
    EXPECT_NEAR(headingDirection(cps[1].headingDeg).x, 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(cps[2].headingDeg, 45.0f);
    EXPECT_TRUE(cps[2].hitByRadius);
    EXPECT_FLOAT_EQ(cps[3].headingDeg, 0.0f);
    // Circuits turn the last one towards the first.
    auto loop = buildCheckpoints(pts, true);
    EXPECT_NEAR(headingDirection(loop[3].headingDeg).x, -1.0f, 1e-5f);
}

// --- Retail races ---------------------------------------------------------------

namespace {

struct Retail {
    vfs::GameSource source;
    city::CityData london, sf;
    Strings strings;
};

Retail* retail() {
    static std::unique_ptr<Retail> r = []() -> std::unique_ptr<Retail> {
        if (!test::gameData())
            return nullptr;
        auto out = std::make_unique<Retail>();
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        if (!src)
            return nullptr;
        out->source = *src;
        auto c = city::loadCity(*test::gameData(), "london");
        auto s = city::loadCity(*test::gameData(), "sf");
        if (!c || !s)
            return nullptr;
        out->london = std::move(*c);
        out->sf = std::move(*s);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> makeSession(GameMode mode, int index, Difficulty diff = Difficulty::Amateur,
                                     int laps = 0, int opponents = 0, const char* city = "london",
                                     SessionOptions options = {}) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = city;
    cfg.raceIndex = index;
    cfg.difficulty = diff;
    cfg.laps = laps;
    cfg.opponents = opponents;
    if (options.seed == 0)
        options.seed = 7;
    std::string error;
    const auto& data = std::string_view(city) == "sf" ? retail()->sf : retail()->london;
    auto s = Session::create(cfg, data, *test::gameData(), retail()->strings, &error, options);
    EXPECT_TRUE(s) << error;
    return s;
}

// Drives the player along points at a given speed, stepping `dt`, with the
// opponent and police states the test sets up. Records all events and when
// they happened.
struct Driver {
    Session& s;
    PlayerState state;
    std::vector<OpponentState> opponents, police;
    std::vector<std::pair<float, Event>> events;
    float time = 0.0f;
    float dt = 1.0f / 30.0f;

    explicit Driver(Session& session) : s(session) {
        state.transform = s.playerSpawn();
        opponents.resize(s.opponents().size());
        for (std::size_t i = 0; i < opponents.size(); ++i)
            opponents[i].transform = s.opponents()[i].spawn;
    }

    void step(float seconds) {
        for (float t = 0; t < seconds; t += dt)
            tick();
    }
    void tick() {
        s.update(dt, state, opponents, police);
        time += dt;
        for (auto& e : s.takeEvents()) {
            events.emplace_back(time, e);
            if (e.type == EventType::DamageReset)
                state.wrecked = false;
            if (e.type == EventType::Respawn)
                state.transform = s.respawnTransform();
        }
    }
    void countdown() {
        s.start();
        for (int i = 0; i < 400 && s.phase() == Phase::Countdown; ++i)
            tick();
    }
    // Drives straight to `target`; stops early when the race is over.
    void driveTo(const Vec3& target, float speed) {
        while (true) {
            const Vec3 p = state.transform.m3;
            Vec3 d = target - p;
            d.y = 0;
            const float len = d.mag();
            const float stepLen = speed * dt;
            const Vec3 dir = len > 1e-4f ? d * (1.0f / len) : -state.transform.m2;
            const Vec3 next = len <= stepLen ? Vec3{target.x, p.y, target.z} : p + dir * stepLen;
            state.transform = carFacing(dir, next);
            state.speedMph = speed * 2.23694f;
            tick();
            if (len <= stepLen || s.phase() != Phase::Racing)
                return;
        }
    }
    int count(EventType t) const {
        int n = 0;
        for (auto& [when, e] : events)
            n += e.type == t;
        return n;
    }
    float when(EventType t) const {
        for (auto& [w, e] : events)
            if (e.type == t)
                return w;
        return -1.0f;
    }
    const Event* find(EventType t) const {
        for (auto& [w, e] : events)
            if (e.type == t)
                return &e;
        return nullptr;
    }
};

} // namespace

TEST(Session, BlitzCountdownCheckpointsAndFinish) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->checkpoints().size(), 5u);
    // Every waypoint but the start is a checkpoint; the last one ends it.
    EXPECT_EQ(s->checkpointsTotal(), 4);
    EXPECT_FLOAT_EQ(s->timeRemaining(), 25.0f); // mmblitzdata.csv, amateur

    Driver d(*s);
    EXPECT_TRUE(s->playerHeld());
    d.countdown();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_FALSE(s->playerHeld());
    // mmSingleBlitz::UpdateGame: "Ready..." until 1.25 s of the 5 s are
    // left, then "Set..." for 1.25 s.
    EXPECT_NEAR(d.when(EventType::CountdownSet) - d.when(EventType::CountdownReady), 3.75f, 0.05f);
    EXPECT_NEAR(d.when(EventType::CountdownGo) - d.when(EventType::CountdownSet), 1.25f, 0.05f);
    EXPECT_EQ(s->message().text, "Go!");
    EXPECT_TRUE(s->message().top);
    EXPECT_FALSE(s->checkpointVisible(0));
    EXPECT_TRUE(s->checkpointVisible(4));

    EXPECT_EQ(s->targetCheckpoint(), 1);
    for (std::size_t i = 1; i < s->checkpoints().size(); ++i)
        d.driveTo(s->checkpoints()[i].position, 30.0f);
    d.step(0.2f);
    EXPECT_EQ(d.count(EventType::CheckpointCleared), 4);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 1);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    // mmSingleBlitz::FinishMessage: string 164, 5 s, placement flag 1.
    EXPECT_EQ(s->message().text, "You Won!");
    EXPECT_TRUE(s->message().top);
    d.step(5.1f);
    EXPECT_TRUE(s->finished());
    const RaceResult r = s->result();
    EXPECT_TRUE(r.finished);
    EXPECT_TRUE(r.won);
    EXPECT_EQ(r.position, 1);
    EXPECT_GT(r.timeSeconds, 5.0f);
    EXPECT_LT(r.timeSeconds, 25.0f);
    // mmGame::CalculateRaceScore: ScoringBias (1 here) x 50 x Difficulty (1).
    EXPECT_EQ(r.score, 50);
}

TEST(Session, BlitzTimeUpLetsTheRaceRunOn) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    Driver d(*s);
    d.countdown();
    d.step(20.0f);
    // Below 10 s: a beep about every second, continuous from 3 s.
    int beeps = 0, loops = 0;
    for (auto& [w, e] : d.events)
        if (e.type == EventType::TimerWarning)
            (e.index == 0 ? beeps : loops) += 1;
    EXPECT_GE(beeps, 4);
    EXPECT_LE(beeps, 6);
    EXPECT_EQ(loops, 0);
    d.step(6.0f);
    EXPECT_EQ(d.count(EventType::TimeUp), 1);
    EXPECT_TRUE(s->timeUp());
    EXPECT_EQ(s->message().text, "Time's up!");
    EXPECT_FALSE(s->message().top);
    // mmSingleBlitz: the race goes on; reaching the end now only ends it.
    EXPECT_EQ(s->phase(), Phase::Racing);
    for (std::size_t i = 1; i < s->checkpoints().size(); ++i)
        d.driveTo(s->checkpoints()[i].position, 40.0f);
    d.step(0.2f);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 0);
    d.step(5.1f);
    EXPECT_TRUE(s->finished());
    EXPECT_FALSE(s->result().won);
    EXPECT_FALSE(s->result().finished);
}

TEST(Session, BlitzWreckAndWaterLoseTheRace) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    {
        auto s = makeSession(GameMode::Blitz, 1);
        ASSERT_TRUE(s);
        Driver d(*s);
        d.countdown();
        d.state.wrecked = true;
        d.tick();
        EXPECT_EQ(s->phase(), Phase::PostRace);
        EXPECT_EQ(s->message().text, "Game over!");
        d.step(5.1f);
        EXPECT_TRUE(s->finished());
        EXPECT_FALSE(s->result().finished);
    }
    {
        auto s = makeSession(GameMode::Blitz, 1);
        ASSERT_TRUE(s);
        Driver d(*s);
        d.countdown();
        d.state.inWater = true;
        d.step(1.0f);
        EXPECT_EQ(d.count(EventType::HitWater), 1);
        EXPECT_EQ(s->message().text, "More tea, vicar?"); // London, string 642
        EXPECT_TRUE(s->message().top);
        EXPECT_EQ(s->phase(), Phase::Racing);
        d.step(4.1f); // HitWaterHandler after 5 s in the water
        EXPECT_EQ(s->phase(), Phase::PostRace);
        d.step(0.6f);
        EXPECT_TRUE(s->finished());
        EXPECT_FALSE(s->result().finished);
        EXPECT_EQ(d.count(EventType::Respawn), 0);
    }
}

TEST(Session, CircuitLapsGatesAndWreckPenalty) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Circuit, 0, Difficulty::Amateur, 2);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->laps(), 2);
    Driver d(*s);
    d.countdown();
    // mmSingleCircuit::UpdateGame: 1.25 s of "Ready...", 1.25 s of "Set...".
    EXPECT_NEAR(d.when(EventType::CountdownGo) - d.when(EventType::CountdownReady), 2.5f, 0.05f);
    // Wrecked: held for 5 s, then repaired; the race goes on.
    d.state.wrecked = true;
    d.tick();
    EXPECT_EQ(d.count(EventType::WreckPenalty), 1);
    EXPECT_EQ(s->message().text, retail()->strings.get(168, "Wait...5 second penalty"));
    EXPECT_TRUE(s->playerHeld());
    d.step(4.8f);
    EXPECT_TRUE(s->playerHeld());
    d.step(0.3f);
    EXPECT_EQ(d.count(EventType::DamageReset), 1);
    EXPECT_FALSE(s->playerHeld());
    EXPECT_EQ(s->phase(), Phase::Racing);

    const auto& cps = s->checkpoints();
    EXPECT_TRUE(s->checkpointVisible(0)); // the start-finish line shows
    for (int lap = 0; lap < 2; ++lap) {
        d.driveTo(cps[1].position, 30.0f);
        // A gate passed hides until the lap is complete.
        EXPECT_FALSE(s->checkpointVisible(1));
        for (std::size_t i = 2; i < cps.size(); ++i)
            d.driveTo(cps[i].position, 30.0f);
        d.driveTo(cps[0].position, 30.0f);
        if (lap == 0) {
            EXPECT_TRUE(s->checkpointVisible(1));
            // mmHUD::PostLapTime: 1 s, lower placement, the lap time under
            // it as GetLocTime's M:SS:HH.
            EXPECT_EQ(s->message().text, "Final lap!");
            EXPECT_FALSE(s->message().top);
            EXPECT_LE(s->message().timeLeft, 1.0f);
            const std::string& t = s->message2().text;
            ASSERT_GE(t.size(), 7u);
            EXPECT_EQ(t[t.size() - 3], ':');
            EXPECT_EQ(t[t.size() - 6], ':');
        }
        d.driveTo(cps[0].position + (cps[1].position - cps[0].position) * 0.2f, 30.0f);
    }
    d.step(0.2f);
    EXPECT_EQ(d.count(EventType::LapCompleted), 2);
    EXPECT_EQ(d.count(EventType::FinalLap), 1);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 1);
    EXPECT_GT(s->bestLapTime(), 0.0f);
    EXPECT_EQ(s->message().text, "You finished 1st!");
    d.step(5.1f);
    const auto r = s->result();
    EXPECT_TRUE(r.finished);
    EXPECT_TRUE(r.won);
    EXPECT_EQ(r.position, 1);
}

TEST(Session, CircuitWaterRespawnsAtTheLastCheckpoint) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Circuit, 0, Difficulty::Amateur, 2);
    ASSERT_TRUE(s);
    Driver d(*s);
    d.countdown();
    d.driveTo(s->checkpoints()[1].position, 30.0f);
    d.driveTo(s->checkpoints()[2].position, 30.0f);
    d.state.inWater = true;
    d.step(5.2f);
    d.state.inWater = false;
    EXPECT_EQ(d.count(EventType::Respawn), 1);
    EXPECT_NEAR(s->respawnTransform().m3.x, s->checkpoints()[2].position.x, 1e-3f);
    EXPECT_EQ(s->phase(), Phase::Racing);
}

TEST(Session, CheckpointRacePlacesAndWinRule) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (auto diff : {Difficulty::Amateur, Difficulty::Professional}) {
        auto s = makeSession(GameMode::Checkpoint, 0, diff, 0, 3);
        ASSERT_TRUE(s);
        ASSERT_EQ(s->opponents().size(), 3u);
        EXPECT_EQ(s->opponents()[0].vehicle, diff == Difficulty::Amateur ? "vpcoop" : "vpcoop2k");
        ASSERT_FALSE(s->opponents()[0].path.empty());
        // aiRouteRacer::Init: on the .opp's first row, at its heading, which
        // lines up with the race (within 10 degrees of the player's start).
        const Mat34& grid = s->opponents()[0].spawn;
        EXPECT_EQ(grid.m3, s->opponents()[0].path.front().position);
        EXPECT_GT((-grid.m2).dot(-s->playerSpawn().m2), std::cos(10.0f * kDegToRad));
        // The finish shows once every checkpoint is cleared; the HUD counts
        // it (mmSingleRace::InitHUD: waypoints - 1).
        EXPECT_FALSE(s->checkpointVisible(s->checkpoints().size() - 1));
        EXPECT_EQ(s->checkpointsTotal(), static_cast<int>(s->checkpoints().size()) - 1);
        Driver d(*s);
        for (auto& o : d.opponents)
            o.transform = s->playerSpawn();
        d.countdown();
        EXPECT_EQ(s->position(), 1);
        // An opponent passing a checkpoint moves ahead (mmWaypoints::AnyWPHits).
        d.opponents[2].transform = carFacing({0, 0, -1}, s->checkpoints()[1].position);
        d.tick();
        EXPECT_EQ(s->position(), 2);
        // Two opponents finish (the AI says so: aiRouteRacer::Finished).
        d.opponents[0].finished = d.opponents[1].finished = true;
        d.tick();
        EXPECT_EQ(d.count(EventType::OpponentFinished), 2);
        EXPECT_EQ(s->message().text, retail()->strings.get(14, "Opponent 2"));
        EXPECT_EQ(s->message2().text, retail()->strings.get(22, "finished 2nd"));
        const auto& cps = s->checkpoints();
        for (std::size_t i = 1; i < cps.size(); ++i)
            d.driveTo(cps[i].position, 40.0f);
        d.step(0.2f);
        ASSERT_EQ(d.count(EventType::PlayerFinished), 1);
        EXPECT_EQ(s->message().text, "You finished 3rd");
        d.step(5.1f);
        const auto r = s->result();
        EXPECT_EQ(r.position, 3);
        // mmSingleRace::ProgressCheck: top three for amateurs, first for professionals.
        EXPECT_EQ(r.won, diff == Difficulty::Amateur);
        EXPECT_EQ(r.score, 10);
    }
}

TEST(Session, CheckpointRaceWreckEndsIt) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Checkpoint, 0);
    ASSERT_TRUE(s);
    Driver d(*s);
    d.countdown();
    d.state.wrecked = true;
    d.tick();
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(s->message().text, "Game over!");
    d.step(5.1f);
    EXPECT_FALSE(s->result().finished);
}

TEST(Session, CruiseWreckWaterAndFallingOut) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Cruise, -1);
    ASSERT_TRUE(s);
    // mmSingleRoam::InitOtherPlayers -> mmGame::RespawnXYZ: 2 m above an AI
    // intersection.
    const city::RoomLocator rooms(retail()->london.psdl, "london");
    ASSERT_TRUE(s->placeRespawnStart(retail()->london, 1, [&](const Vec3& p) { return rooms.find(p); }));
    bool atIntersection = false;
    for (const auto& x : retail()->london.aiMap->intersections)
        atIntersection |= s->playerSpawn().m3.dist(x.center + Vec3{0, 2, 0}) < 1e-3f;
    EXPECT_TRUE(atIntersection);
    Driver d(*s);
    s->start();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_FALSE(s->playerHeld());
    // mmSingleRoam::UpdateGame: a wreck parks the car for 3 s, then repairs it.
    d.state.wrecked = true;
    d.tick();
    EXPECT_TRUE(s->playerHeld());
    d.step(3.1f);
    EXPECT_EQ(d.count(EventType::DamageReset), 1);
    EXPECT_FALSE(s->playerHeld());
    // Five seconds in the water restart the cruise.
    d.state.inWater = true;
    d.step(1.0f);
    EXPECT_EQ(d.count(EventType::HitWater), 1);
    EXPECT_EQ(d.count(EventType::Restart), 0);
    d.step(4.2f);
    EXPECT_EQ(d.count(EventType::Restart), 1);
    d.state.inWater = false;
    // Falling through the city resets the game too (mmGame::DropThruCityHandler).
    d.state.transform.m3.y = -60.0f;
    d.tick();
    EXPECT_EQ(d.count(EventType::Restart), 2);
}

TEST(Session, CrashCourseMinimumSpeed) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    // London lesson 2 ("Cutting Corners"): corner.csv, keep 40 mph.
    for (float mph : {30.0f, 55.0f}) {
        auto s = makeSession(GameMode::CrashCourse, 1);
        ASSERT_TRUE(s);
        ASSERT_TRUE(s->currentLesson());
        EXPECT_EQ(s->currentLesson()->type, LessonType::MinimumSpeed);
        EXPECT_FLOAT_EQ(s->currentLesson()->minimumSpeedMph, 40.0f);
        EXPECT_LT(s->timeRemaining(), 0.0f); // no time limit
        Driver d(*s);
        d.countdown();
        for (std::size_t i = 1; i < s->checkpoints().size() && s->phase() == Phase::Racing; ++i)
            d.driveTo(s->checkpoints()[i].position, mph / 2.23694f);
        d.step(0.2f);
        if (mph < 40.0f) {
            EXPECT_EQ(s->message().text, "You need to get up to speed");
        }
        d.step(6.0f);
        EXPECT_TRUE(s->finished());
        EXPECT_EQ(s->result().won, mph > 40.0f) << mph;
        EXPECT_EQ(d.count(EventType::LessonFailed), mph > 40.0f ? 0 : 1);
    }
    // Dropping below the speed for more than a second fails.
    auto s = makeSession(GameMode::CrashCourse, 1);
    Driver d(*s);
    d.countdown();
    d.driveTo(s->checkpoints()[1].position, 55.0f / 2.23694f);
    d.state.speedMph = 30.0f;
    d.step(0.9f);
    EXPECT_EQ(s->phase(), Phase::Racing);
    d.step(0.2f);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(d.count(EventType::LessonFailed), 1);
}

TEST(Session, CrashCourseExamContinuesWithoutRespawn) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    // London Midterm 2 (crash7): a timed course, then follow the cab.
    auto s = makeSession(GameMode::CrashCourse, 7);
    ASSERT_TRUE(s);
    ASSERT_EQ(s->setup().lessonEvents.size(), 2u);
    EXPECT_EQ(s->setup().lessonEvents[0].type, LessonType::Course);
    EXPECT_EQ(s->setup().lessonEvents[1].type, LessonType::Follow);
    EXPECT_EQ(s->setup().lessonEvents[1].opponents, 1);
    Driver d(*s);
    d.countdown();
    // The lesson's name, then 1.25 s "Ready..." and "Set..." (UpdateBlitz).
    EXPECT_FALSE(s->opponentActive(0));
    for (std::size_t i = 1; i < s->checkpoints().size(); ++i)
        d.driveTo(s->checkpoints()[i].position, 25.0f);
    d.step(0.2f);
    // mmSingleStunt::InitNewEvent: the next event starts where the car is.
    EXPECT_EQ(s->lessonEvent(), 1);
    EXPECT_EQ(d.count(EventType::LessonPassed), 0);
    EXPECT_EQ(d.count(EventType::Respawn), 0);
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_TRUE(s->opponentActive(0));
    EXPECT_FALSE(s->finished());
    // The cab arrives with the player 5 m behind: passed.
    d.opponents[0].transform = carFacing({0, 0, -1}, d.state.transform.m3 + Vec3{5, 0, 0});
    d.opponents[0].finished = true;
    d.tick();
    EXPECT_EQ(d.count(EventType::LessonPassed), 1);
    d.step(5.1f);
    EXPECT_TRUE(s->result().won);
}

TEST(Session, CrashCourseFollowEscape) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::CrashCourse, 6); // London "Follow That Car!"
    ASSERT_TRUE(s);
    EXPECT_EQ(s->currentLesson()->type, LessonType::Follow);
    EXPECT_LT(s->timeRemaining(), 0.0f); // the chase has no clock
    Driver d(*s);
    d.countdown();
    EXPECT_TRUE(s->opponentActive(0));
    d.opponents[0].transform = carFacing({0, 0, -1}, d.state.transform.m3 + Vec3{0, 0, -99});
    d.tick();
    EXPECT_EQ(s->phase(), Phase::Racing);
    d.opponents[0].transform.m3 = d.state.transform.m3 + Vec3{0, 0, -101};
    d.tick();
    EXPECT_EQ(s->phase(), Phase::PostRace);
    EXPECT_EQ(s->message().text, "Car escaped!");
    EXPECT_EQ(s->message2().text, "Game over");
}

TEST(Session, CrashCourseEvadeNeedsNoPursuers) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (bool chased : {true, false}) {
        auto s = makeSession(GameMode::CrashCourse, 10); // London "The Heat Is On"
        ASSERT_TRUE(s);
        EXPECT_EQ(s->currentLesson()->type, LessonType::Evade);
        EXPECT_TRUE(s->policeActive());
        Driver d(*s);
        d.police.resize(s->police().size());
        for (std::size_t i = 0; i < d.police.size(); ++i)
            d.police[i].transform = s->police()[i].spawn;
        d.countdown();
        for (std::size_t i = 1; i < s->checkpoints().size() && s->phase() == Phase::Racing; ++i) {
            if (i + 1 == s->checkpoints().size() && chased) {
                d.police[0].pursuing = true;
                d.police[0].transform.m3 = s->checkpoints()[i].position + Vec3{0, 0, 150};
            }
            d.driveTo(s->checkpoints()[i].position, 60.0f);
        }
        d.step(0.2f);
        EXPECT_EQ(s->phase(), Phase::PostRace);
        EXPECT_EQ(s->message().text,
                  chased ? "Lose your pursuers before you finish!" : "You survived the gauntlet!");
        d.step(5.1f);
        EXPECT_EQ(s->result().won, !chased);
    }
}

TEST(Session, CrashCourseCleanAndDestroySetDamageLimits) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    {
        auto s = makeSession(GameMode::CrashCourse, 8, Difficulty::Amateur, 0, 0, "sf"); // frogger
        ASSERT_TRUE(s);
        EXPECT_EQ(s->currentLesson()->type, LessonType::Clean);
        Driver d(*s);
        d.countdown();
        const Event* limits = d.find(EventType::PlayerDamageLimits);
        ASSERT_TRUE(limits);
        EXPECT_FLOAT_EQ(limits->value, 10.0f);
        d.state.wrecked = true;
        d.tick();
        EXPECT_EQ(s->message().text, "You scraped the paint!");
        EXPECT_EQ(d.count(EventType::LessonFailed), 1);
    }
    {
        auto s = makeSession(GameMode::CrashCourse, 6, Difficulty::Amateur, 0, 0, "sf"); // stop
        ASSERT_TRUE(s);
        EXPECT_EQ(s->currentLesson()->type, LessonType::Destroy);
        Driver d(*s);
        d.countdown();
        const Event* limits = d.find(EventType::OpponentDamageLimits);
        ASSERT_TRUE(limits);
        EXPECT_EQ(limits->index, 0);
        EXPECT_FLOAT_EQ(limits->value, 150000.0f);
        d.opponents[0].currentDamage = 149000.0f;
        d.tick();
        EXPECT_EQ(s->phase(), Phase::Racing);
        d.opponents[0].currentDamage = 150000.0f;
        d.tick();
        EXPECT_EQ(s->message().text, "You did it!");
        EXPECT_FALSE(s->opponentActive(0));
        d.step(5.1f);
        EXPECT_TRUE(s->result().won);
    }
}

TEST(Session, CrashCourseJumpAnyOrderEndsAtTheLast) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::CrashCourse, 0); // London "Frequent Flyer": longjump.csv
    ASSERT_TRUE(s);
    EXPECT_EQ(s->currentLesson()->type, LessonType::Jump);
    const auto& cps = s->checkpoints();
    ASSERT_EQ(cps.size(), 5u);
    Driver d(*s);
    d.countdown();
    // The last one does nothing until the others are cleared.
    d.state.transform = carFacing({0, 0, -1}, cps[4].position + Vec3{0, 0, 30});
    d.tick();
    d.driveTo(cps[4].position, 30.0f);
    EXPECT_EQ(d.count(EventType::CheckpointCleared), 0);
    for (int i : {3, 2, 1}) {
        d.state.transform = carFacing({0, 0, -1}, cps[static_cast<std::size_t>(i)].position + Vec3{0, 0, 30});
        d.tick();
        d.driveTo(cps[static_cast<std::size_t>(i)].position, 30.0f);
    }
    EXPECT_EQ(d.count(EventType::CheckpointCleared), 3);
    EXPECT_EQ(s->phase(), Phase::Racing);
    d.state.transform = carFacing({0, 0, -1}, cps[4].position + Vec3{0, 0, 30});
    d.tick();
    d.driveTo(cps[4].position, 30.0f);
    d.step(0.2f);
    EXPECT_EQ(d.count(EventType::LessonPassed), 1);
    EXPECT_EQ(s->message().text, "Good jumping!");
}

TEST(Session, EveryRetailEventLoads) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (const char* cityName : {"london", "sf"}) {
        const auto& c = std::string_view(cityName) == "sf" ? retail()->sf : retail()->london;
        int n = 0;
        for (const auto& race : c.races) {
            RaceConfig cfg;
            cfg.city = cityName;
            cfg.raceIndex = race.index;
            cfg.opponents = 8;
            switch (race.mode) {
            case city::RaceMode::Blitz: cfg.mode = GameMode::Blitz; break;
            case city::RaceMode::Circuit: cfg.mode = GameMode::Circuit; break;
            case city::RaceMode::Checkpoint: cfg.mode = GameMode::Checkpoint; break;
            case city::RaceMode::CrashCourse: cfg.mode = GameMode::CrashCourse; break;
            }
            for (auto diff : {Difficulty::Amateur, Difficulty::Professional}) {
                cfg.difficulty = diff;
                std::string error;
                auto s = Session::create(cfg, c, *test::gameData(), retail()->strings, &error, {.seed = 3});
                ASSERT_TRUE(s) << cityName << " " << city::raceModeName(race.mode) << race.index << ": " << error;
                EXPECT_GE(s->checkpoints().size(), 2u);
                for (const auto& e : s->setup().lessonEvents) {
                    // Retail lessons use every type but 1 and 6.
                    EXPECT_NE(e.type, LessonType::Collide);
                    EXPECT_NE(e.type, LessonType::Acceleration);
                    // "numopp" never asks for more cars than the aimap has.
                    EXPECT_LE(e.opponents, static_cast<int>(s->opponents().size()));
                }
                ++n;
            }
        }
        EXPECT_EQ(n, 2 * 45) << cityName;
    }
}

#include "game/session/CopsAndRobbers.h"

TEST(CopsAndRobbers, SetsPickupDropAndDelivery) {
    CrLocations loc;
    for (int i = 0; i < 10; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    CrSettings set;
    set.mode = CopsAndRobbersMode::CopsVsRobbers;
    set.pointLimit = 200;
    set.goldMass = 2;
    CopsAndRobbers cr(set, loc);
    EXPECT_FLOAT_EQ(cr.carrierExtraMassKg(), 200.0f);
    EXPECT_FLOAT_EQ(cr.carrierThrottleCap(), 0.81f);
    // Bank, gold and hideout on different places of the pool, never its last row.
    const CrSet first = cr.set();
    EXPECT_NE(first.bank, first.gold);
    EXPECT_NE(first.hideout, first.gold);
    EXPECT_NE(first.hideout, first.bank);
    for (const Vec3& p : {first.bank, first.gold, first.hideout})
        EXPECT_NE(p, loc.points.back());
    cr.addCar(1, CrTeam::Robber);
    cr.addCar(2, CrTeam::Cop);

    std::vector<CopsAndRobbers::Car> cars{{1, CrTeam::Robber, first.gold + Vec3{4.9f, 0, 0}, false, false},
                                          {2, CrTeam::Cop, {5000, 0, 0}, false, false}};
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), 1);
    EXPECT_EQ(cr.playerScore(1), 25); // picking it up scores
    // A hard hit knocks it loose where the carrier is; the cop must pick it up.
    cars[1].position = cars[0].position + Vec3{3, 0, 0};
    cr.update(0.1f, cars, {{2, 1, 249.0f}});
    EXPECT_EQ(cr.goldCarrier(), 1);
    cr.update(0.1f, cars, {{2, 1, 250.0f}});
    EXPECT_EQ(cr.goldCarrier(), -1);
    EXPECT_EQ(cr.goldPosition(), cars[0].position);
    // The robber who lost it cannot take it back for 2 s; the cop is 3 m away.
    cars[1].position = {5000, 0, 0};
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), -1);
    cr.update(2.0f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), 1);
    // Robbers deliver to the hideout.
    cars[0].position = cr.set().hideout + Vec3{11.9f, 0, 0};
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.playerScore(1), 25 + 25 + 100);
    EXPECT_EQ(cr.score(CrTeam::Robber), 150);
    EXPECT_EQ(cr.goldCarrier(), -1);
    // A wrecked carrier drops it; a carrier in the water sends it home.
    cars[0].position = cr.set().gold;
    cr.update(0.1f, cars, {});
    ASSERT_EQ(cr.goldCarrier(), 1);
    cars[0].position += Vec3{50, 0, 0};
    cars[0].inWater = true;
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), -1);
    EXPECT_EQ(cr.goldPosition(), cr.set().gold);
    // Team modes end at the team's point limit.
    cars[0].inWater = false;
    cars[0].position = cr.set().gold;
    cr.update(3.0f, cars, {});
    cars[0].position = cr.set().hideout;
    cr.update(0.1f, cars, {});
    EXPECT_TRUE(cr.over());
}

TEST(CopsAndRobbers, TimeWarningsAndFreeForAllLimit) {
    CrLocations loc;
    for (int i = 0; i < 5; ++i)
        loc.points.push_back({100.0f * static_cast<float>(i), 0, 0});
    CrSettings timed;
    timed.timeLimitSeconds = 600.0f;
    CopsAndRobbers t(timed, loc);
    for (int i = 0; i < 6000 + 5; ++i)
        t.update(0.1f, {}, {});
    EXPECT_TRUE(t.over());
    std::vector<int> warnings;
    for (auto& e : t.takeEvents())
        if (e.type == CopsAndRobbers::EventType::TimeWarning)
            warnings.push_back(e.value);
    // A 10 minute game warns at 5 and 1 minutes (10 is where it starts).
    EXPECT_EQ(warnings, (std::vector<int>{5, 1}));

    CrSettings ffa;
    ffa.pointLimit = 100;
    CopsAndRobbers f(ffa, loc);
    f.addCar(1, CrTeam::Robber);
    f.addCar(2, CrTeam::Robber);
    std::vector<CopsAndRobbers::Car> cars{{1, CrTeam::Robber, f.set().gold, false, false}};
    f.update(0.1f, cars, {});
    cars[0].position = f.set().hideout;
    f.update(0.1f, cars, {});
    EXPECT_FALSE(f.over()); // UpdateLimit runs before the frame's UpdateHideout
    f.update(0.1f, cars, {});
    EXPECT_TRUE(f.over()); // 125 points for one player
}

TEST(CopsAndRobbers, RetailLocations) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* c : {"london", "sf"}) {
        auto loc = loadCrLocations(*test::gameData(), c);
        ASSERT_TRUE(loc) << c;
        EXPECT_GE(loc->points.size(), 40u);
    }
}

#include "asset/Image.h"
#include "asset/Pkg.h"
#include "city/CityMesh.h"

#include <cstdio>

// The overhead map model (geometry/hudmap_london.pkg) is a flat mesh in world
// coordinates: sampling its textures at the Thames' PSDL water rooms must give
// water blue.
TEST(SessionHudMap, MapModelMatchesTheCity) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    const auto& vfs = *test::gameData();
    auto bytes = vfs.readAll("geometry/hudmap_london.pkg");
    ASSERT_TRUE(bytes);
    auto pkg = asset::parsePkg(*bytes);
    ASSERT_TRUE(pkg);
    const auto* mesh = pkg->find("", asset::Lod::High);
    ASSERT_TRUE(mesh);

    // Texture lookup per material.
    std::map<std::uint32_t, asset::Image> images;
    for (const auto& section : mesh->sections) {
        const auto& mat = pkg->paintjobs[0][section.shaderIndex];
        auto tb = vfs.readAll("texture/" + str::lower(mat.texture) + ".tex");
        ASSERT_TRUE(tb) << mat.texture;
        auto tex = asset::parseTex(*tb);
        ASSERT_TRUE(tex);
        images[section.shaderIndex] = tex->image;
    }
    auto sample = [&](float x, float z) -> std::optional<std::array<int, 3>> {
        for (const auto& section : mesh->sections) {
            for (const auto& packet : section.packets) {
                for (std::size_t i = 0; i + 2 < packet.indices.size(); i += 3) {
                    const auto& a = packet.vertices[packet.indices[i]];
                    const auto& b = packet.vertices[packet.indices[i + 1]];
                    const auto& c = packet.vertices[packet.indices[i + 2]];
                    const Vec2 p{x, z}, pa{a.position.x, a.position.z}, pb{b.position.x, b.position.z},
                        pc{c.position.x, c.position.z};
                    const float d = (pb - pa).cross(pc - pa);
                    if (std::abs(d) < 1e-6f)
                        continue;
                    const float u = (pb - p).cross(pc - p) / d, v = (pc - p).cross(pa - p) / d, w = 1.0f - u - v;
                    if (u < -1e-4f || v < -1e-4f || w < -1e-4f)
                        continue;
                    const Vec2 uv = a.uv * u + b.uv * v + c.uv * w;
                    const auto& img = images[section.shaderIndex].levels[0];
                    const auto tx = static_cast<std::uint32_t>(std::clamp(uv.x, 0.0f, 0.9999f) * img.width);
                    const auto ty = static_cast<std::uint32_t>(std::clamp(uv.y, 0.0f, 0.9999f) * img.height);
                    const std::size_t o = (static_cast<std::size_t>(ty) * img.width + tx) * 4;
                    return std::array<int, 3>{img.rgba[o], img.rgba[o + 1], img.rgba[o + 2]};
                }
            }
        }
        return std::nullopt;
    };
    const auto& city = retail()->london;
    ASSERT_TRUE(city.water);
    // Sample the water rooms' triangle centroids (from the street mesh) and
    // compare with the mirrored placements, which must fit far worse.
    auto isBlue = [](const std::array<int, 3>& c) { return c[2] > c[0] + 60 && c[2] > 150; };
    const city::CityMesh cm = city::buildCityMesh(city.psdl);
    std::vector<Vec3> points;
    for (const auto& room : cm.rooms)
        for (const auto& b : room.batches) {
            const std::string* tex = city.psdl.texture(b.texture);
            if (!tex || tex->find("thames") == std::string::npos)
                continue;
            for (std::size_t i = 0; i + 2 < b.indices.size(); i += 3)
                points.push_back((b.vertices[b.indices[i]].position + b.vertices[b.indices[i + 1]].position +
                                  b.vertices[b.indices[i + 2]].position) *
                                 (1.0f / 3.0f));
        }
    ASSERT_GT(points.size(), 10u);
    auto fraction = [&](float sx, float sz) {
        int blue = 0, total = 0;
        for (const auto& p : points) {
            if (auto c = sample(p.x * sx, p.z * sz)) {
                ++total;
                blue += isBlue(*c);
            }
        }
        return total ? static_cast<float>(blue) / static_cast<float>(total) : 0.0f;
    };
    const float straight = fraction(1, 1);
    EXPECT_GT(straight, 0.8f);
    EXPECT_LT(fraction(-1, 1), straight - 0.3f);
    EXPECT_LT(fraction(1, -1), straight - 0.3f);
    std::printf("water on the map: %.2f (mirrored x %.2f, mirrored z %.2f)\n", straight, fraction(-1, 1),
                fraction(1, -1));
}
