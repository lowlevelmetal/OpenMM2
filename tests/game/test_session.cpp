#include "TestData.h"
#include "city/CityData.h"
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

TEST(SessionGate, CrossingAndMissing) {
    const Checkpoint cp = makeCheckpoint({0, 0, 0}, 0.0f, 5.0f); // gate from x=-5 to x=5 at z=0
    Mat34 car = Mat34::identity();
    car.m3 = {2, 0, -3}; // drove from z=+3 to z=-3 at x=2
    EXPECT_TRUE(gateHit(cp, {2, 0, 3}, car));
    car.m3 = {12, 0, -3}; // beside the gate
    EXPECT_FALSE(gateHit(cp, {12, 0, 3}, car));
    car.m3 = {2, 0, 30}; // far away, not moving
    EXPECT_FALSE(gateHit(cp, {2, 0, 30}, car));
    // Parked across the gate: the car's long axis crosses it.
    car.m3 = {0, 0, 0.5f};
    EXPECT_TRUE(gateHit(cp, {0, 0, 0.5f}, car));
    EXPECT_TRUE(lineIntersect({0, -1}, {0, 1}, {-1, 0}, {1, 0}, 0.0f));
    EXPECT_FALSE(lineIntersect({0, 1}, {0, 2}, {-1, 0}, {1, 0}, 0.0f));
}

// --- Retail races ---------------------------------------------------------------

namespace {

struct Retail {
    vfs::GameSource source;
    city::CityData london;
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
        if (!c)
            return nullptr;
        out->london = std::move(*c);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> makeSession(GameMode mode, int index, Difficulty diff = Difficulty::Amateur, int laps = 0,
                                     int opponents = 0) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "london";
    cfg.raceIndex = index;
    cfg.difficulty = diff;
    cfg.laps = laps;
    cfg.opponents = opponents;
    std::string error;
    auto s = Session::create(cfg, retail()->london, *test::gameData(), retail()->strings, &error);
    EXPECT_TRUE(s) << error;
    return s;
}

// Drives the player along `points` at `speed` m/s, stepping `dt`, starting
// after the countdown. Returns all events.
struct Driver {
    Session& s;
    PlayerState state;
    std::vector<Event> events;
    float dt = 1.0f / 30.0f;

    explicit Driver(Session& session) : s(session) { state.transform = s.playerSpawn(); }

    void step(float seconds) {
        for (float t = 0; t < seconds; t += dt)
            tick();
    }
    void tick() {
        s.update(dt, state);
        for (auto& e : s.takeEvents())
            events.push_back(e);
    }
    void countdown() {
        s.start();
        for (int i = 0; i < 200 && s.phase() == Phase::Countdown; ++i)
            tick();
    }
    void driveTo(const Vec3& target, float speed) {
        while (true) {
            const Vec3 p = state.transform.m3;
            Vec3 d = target - p;
            d.y = 0;
            const float len = d.mag();
            const float stepLen = speed * dt;
            Vec3 dir = len > 1e-4f ? d * (1.0f / len) : -state.transform.m2;
            Mat34 m = Camera3(dir);
            m.m3 = len <= stepLen ? Vec3{target.x, p.y, target.z} : p + dir * stepLen;
            state.transform = m;
            state.speedMph = speed * 2.23694f;
            tick();
            if (len <= stepLen || s.phase() != Phase::Racing)
                return;
        }
    }
    static Mat34 Camera3(const Vec3& forward) {
        Mat34 m;
        m.m2 = -forward;
        m.m1 = Vec3::yAxis();
        m.m0 = m.m1.cross(m.m2).normalized();
        return m;
    }
    int count(EventType t) const {
        int n = 0;
        for (auto& e : events)
            n += e.type == t;
        return n;
    }
};

} // namespace

TEST(Session, BlitzCountdownCheckpointsAndFinish) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->checkpoints().size(), 5u);
    EXPECT_EQ(s->checkpointsTotal(), 3);
    EXPECT_FLOAT_EQ(s->timeRemaining(), 25.0f); // mmblitzdata.csv, amateur

    Driver d(*s);
    EXPECT_TRUE(s->playerHeld());
    d.countdown();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_FALSE(s->playerHeld());
    EXPECT_EQ(d.count(EventType::CountdownReady), 1);
    EXPECT_EQ(d.count(EventType::CountdownSet), 1);
    EXPECT_EQ(d.count(EventType::CountdownGo), 1);

    // The arrow points at the nearest checkpoint (waypoint 1).
    EXPECT_EQ(s->targetCheckpoint(), 1);
    for (std::size_t i = 1; i < s->checkpoints().size(); ++i)
        d.driveTo(s->checkpoints()[i].position, 30.0f);
    EXPECT_EQ(d.count(EventType::CheckpointCleared), 3);
    EXPECT_EQ(d.count(EventType::FinishActivated), 1);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 1);
    EXPECT_EQ(s->phase(), Phase::PostRace);
    d.step(6.0f);
    EXPECT_TRUE(s->finished());
    const RaceResult r = s->result();
    EXPECT_TRUE(r.finished);
    EXPECT_TRUE(r.won);
    EXPECT_EQ(r.position, 1);
    EXPECT_GT(r.timeSeconds, 10.0f);
    EXPECT_LT(r.timeSeconds, 25.0f);
}

TEST(Session, BlitzFinishNeedsAllCheckpointsAndTimesOut) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Blitz, 0);
    ASSERT_TRUE(s);
    Driver d(*s);
    d.countdown();
    // To the finish from the side, skipping the checkpoints: it does not count.
    d.state.transform.m3 = s->checkpoints().back().position + Vec3{60, 0, 0};
    d.tick();
    d.driveTo(s->checkpoints().back().position, 40.0f);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 0);
    EXPECT_FALSE(s->checkpointVisible(s->checkpoints().size() - 1));
    d.step(30.0f);
    EXPECT_EQ(d.count(EventType::TimeUp), 1);
    EXPECT_GE(d.count(EventType::TimerWarning), 9);
    d.step(6.0f);
    EXPECT_TRUE(s->finished());
    EXPECT_FALSE(s->result().won);
    EXPECT_FALSE(s->result().finished);
}

TEST(Session, CircuitLapsAndFalseStart) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Circuit, 0, Difficulty::Amateur, 2);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->laps(), 2);
    Driver d(*s);
    d.state.throttle = 1.0f; // jumping the start
    d.countdown();
    d.state.throttle = 0.0f;
    EXPECT_EQ(d.count(EventType::FalseStart), 1);
    EXPECT_TRUE(s->playerHeld());
    d.step(5.1f);
    EXPECT_FALSE(s->playerHeld());
    EXPECT_EQ(d.count(EventType::PenaltyOver), 1);

    const auto& cps = s->checkpoints();
    for (int lap = 0; lap < 2; ++lap) {
        for (std::size_t i = 1; i < cps.size(); ++i)
            d.driveTo(cps[i].position, 30.0f);
        d.driveTo(cps[0].position, 30.0f);
        // Overshoot the start line so the next lap begins past it.
        d.driveTo(cps[0].position + (cps[1].position - cps[0].position) * 0.2f, 30.0f);
    }
    EXPECT_EQ(d.count(EventType::LapCompleted), 2);
    EXPECT_EQ(d.count(EventType::FinalLap), 1);
    EXPECT_EQ(d.count(EventType::PlayerFinished), 1);
    EXPECT_GT(s->bestLapTime(), 0.0f);
    d.step(6.0f);
    const auto r = s->result();
    EXPECT_TRUE(r.finished);
    EXPECT_TRUE(r.won);
    EXPECT_EQ(r.position, 1);
}

TEST(Session, CheckpointRacePositions) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (auto diff : {Difficulty::Amateur, Difficulty::Professional}) {
        auto s = makeSession(GameMode::Checkpoint, 0, diff, 0, 3);
        ASSERT_TRUE(s);
        ASSERT_EQ(s->opponents().size(), 3u);
        EXPECT_EQ(s->opponents()[0].vehicle, diff == Difficulty::Amateur ? "vpcoop" : "vpcoop2k");
        EXPECT_FALSE(s->opponents()[0].path.empty());
        Driver d(*s);
        d.countdown();
        // Two opponents teleport over the course (they finish first), the
        // third parks; the player then finishes third.
        std::vector<OpponentState> opp(3);
        for (auto& o : opp)
            o.transform = s->playerSpawn();
        const auto& cps = s->checkpoints();
        for (int which = 0; which < 2; ++which) {
            for (std::size_t i = 1; i < cps.size(); ++i) {
                opp[static_cast<std::size_t>(which)].transform.m3 = cps[i].position;
                s->update(d.dt, d.state, opp);
            }
        }
        EXPECT_EQ(s->position(), 3);
        d.events.clear();
        auto oppTick = [&] { s->update(d.dt, d.state, opp); };
        (void)oppTick;
        for (std::size_t i = 1; i < cps.size(); ++i)
            d.driveTo(cps[i].position, 40.0f);
        ASSERT_EQ(d.count(EventType::PlayerFinished), 1);
        d.step(6.0f);
        const auto r = s->result();
        EXPECT_EQ(r.position, 3);
        EXPECT_EQ(r.won, diff == Difficulty::Amateur); // MustPlace 3; professionals must win
    }
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
        Driver d(*s);
        d.countdown();
        for (std::size_t i = 1; i < s->checkpoints().size() && s->phase() == Phase::Racing; ++i)
            d.driveTo(s->checkpoints()[i].position, mph / 2.23694f);
        d.step(6.0f);
        EXPECT_TRUE(s->finished());
        EXPECT_EQ(s->result().won, mph > 40.0f) << mph;
        EXPECT_EQ(d.count(EventType::LessonFailed), mph > 40.0f ? 0 : 1);
    }
}

TEST(Session, CrashCourseExamRunsBothEvents) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    // London Midterm 2 (crash7): a timed course, then follow the cab.
    auto s = makeSession(GameMode::CrashCourse, 7);
    ASSERT_TRUE(s);
    ASSERT_EQ(s->setup().lessonEvents.size(), 2u);
    EXPECT_EQ(s->setup().lessonEvents[0].type, LessonType::Course);
    EXPECT_EQ(s->setup().lessonEvents[1].type, LessonType::Follow);
    Driver d(*s);
    d.countdown();
    for (std::size_t i = 1; i < s->checkpoints().size(); ++i)
        d.driveTo(s->checkpoints()[i].position, 25.0f);
    EXPECT_EQ(d.count(EventType::LessonPassed), 1);
    d.step(5.5f);
    EXPECT_EQ(s->lessonEvent(), 1);
    EXPECT_EQ(d.count(EventType::Respawn), 1);
    EXPECT_FALSE(s->finished());
}

TEST(Session, WaterAndFallingOutRespawn) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    auto s = makeSession(GameMode::Cruise, -1);
    ASSERT_TRUE(s);
    Driver d(*s);
    s->start();
    EXPECT_EQ(s->phase(), Phase::Racing);
    EXPECT_FALSE(s->playerHeld());
    d.state.inWater = true;
    d.step(1.0f);
    EXPECT_EQ(d.count(EventType::HitWater), 1);
    EXPECT_EQ(d.count(EventType::Respawn), 0);
    d.step(3.5f);
    EXPECT_EQ(d.count(EventType::Respawn), 1);
    d.state.inWater = false;
    d.state.transform.m3.y = -500.0f;
    d.tick();
    EXPECT_EQ(d.count(EventType::Respawn), 2);
    EXPECT_FALSE(s->message().text.empty());
}

TEST(Session, EveryRetailEventLoads) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (const char* cityName : {"london", "sf"}) {
        auto c = city::loadCity(*test::gameData(), cityName);
        ASSERT_TRUE(c);
        int n = 0;
        for (const auto& race : c->races) {
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
                auto s = Session::create(cfg, *c, *test::gameData(), retail()->strings, &error);
                ASSERT_TRUE(s) << cityName << " " << city::raceModeName(race.mode) << race.index << ": " << error;
                EXPECT_GE(s->checkpoints().size(), 2u);
                ++n;
            }
        }
        EXPECT_EQ(n, 2 * 45) << cityName;
    }
}

#include "game/session/CopsAndRobbers.h"

TEST(CopsAndRobbers, PickupStealDeliverAndLimits) {
    CrLocations loc;
    loc.bank = {0, 0, 0};
    loc.hideout = {500, 0, 0};
    loc.gold = {{100, 0, 0}, {200, 0, 0}};
    CrSettings set;
    set.mode = CopsAndRobbersMode::CopsVsRobbers;
    set.pointLimit = 2;
    CopsAndRobbers cr(set, loc);
    cr.addCar(1, CrTeam::Robber);
    cr.addCar(2, CrTeam::Cop);
    const Vec3 gold = cr.goldPosition();

    std::vector<CopsAndRobbers::Car> cars{{1, CrTeam::Robber, gold, false}, {2, CrTeam::Cop, {50, 0, 0}, false}};
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), 1);
    // The cop rams the robber and takes the gold, then returns it to the bank.
    cr.update(0.1f, cars, {{2, 1, 5000.0f}});
    EXPECT_EQ(cr.goldCarrier(), 2);
    cars[1].position = loc.bank;
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.score(CrTeam::Cop), 1);
    EXPECT_EQ(cr.goldCarrier(), -1);
    // Robber delivers to the hideout.
    cars[0].position = cr.goldPosition();
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), 1);
    cars[0].position = loc.hideout;
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.score(CrTeam::Robber), 1);
    EXPECT_FALSE(cr.over());
    // Wrecked carriers drop the gold.
    cars[0].position = cr.goldPosition();
    cr.update(0.1f, cars, {});
    cars[0].wrecked = true;
    cr.update(0.1f, cars, {});
    EXPECT_EQ(cr.goldCarrier(), -1);
    bool dropped = false;
    for (auto& e : cr.takeEvents())
        dropped |= e.type == CopsAndRobbers::EventType::GoldDropped;
    EXPECT_TRUE(dropped);

    CrSettings timed;
    timed.timeLimitSeconds = 300.0f;
    CopsAndRobbers t(timed, loc);
    for (int i = 0; i < 300 * 10 + 5; ++i)
        t.update(0.1f, {}, {});
    EXPECT_TRUE(t.over());
    int warnings = 0;
    for (auto& e : t.takeEvents())
        warnings += e.type == CopsAndRobbers::EventType::TimeWarning;
    EXPECT_EQ(warnings, 1); // "1 minute remaining"
}

TEST(CopsAndRobbers, RetailLocations) {
    MM2_REQUIRE_GAME_DATA();
    for (const char* c : {"london", "sf"}) {
        auto loc = loadCrLocations(*test::gameData(), c);
        ASSERT_TRUE(loc) << c;
        EXPECT_GE(loc->gold.size(), 40u);
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
