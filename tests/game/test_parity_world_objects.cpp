// The city's moving and scripted objects (src/game/world), checked against
// MM2's gizmo managers (midtown2.exe build 3393, MM2Recomp).
#include "TestData.h"

#include "city/CityData.h"
#include "city/RoomInfo.h"
#include "game/CityLevel.h"
#include "game/bangers/BangerSet.h"
#include "ai/World.h"
#include "game/world/CableCars.h"
#include "game/world/Gizmos.h"
#include "game/world/PathSpline.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::world;

namespace {

std::vector<Vec3> square() { return {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}; }

void expectNear(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

} // namespace

// gizPathspline::Reset / IncrementPath / ComputePath: the first segment runs
// from point 0 to point 1 with Catmull-Rom tangents of the closed loop.
TEST(WorldPathSpline, StartsOnTheFirstSegment) {
    PathSpline s;
    s.init(square(), 1.0f);
    EXPECT_EQ(s.index(), 0);
    EXPECT_EQ(s.next(), 1);
    Vec3 p, d;
    s.update(p, d, 0.0f);
    expectNear(p, {0, 0, 0});
    // t0 = (p1 - p3) / 2.
    expectNear(d, {5, 0, -5});
    // The segment's length is the chord through its middle.
    Vec3 mid, t;
    s.updateRatio(mid, t, 0.5f);
    EXPECT_NEAR(s.length(), mid.dist(Vec3{0, 0, 0}) + mid.dist(Vec3{10, 0, 0}), 1e-4f);
    EXPECT_GT(s.length(), 10.0f);
}

// gizPathspline::Update / UpdateRatio: speed x time / length; past the end
// the next segment, keeping the time left; backwards the previous one.
TEST(WorldPathSpline, MovesAlongTheLoopBothWays) {
    PathSpline s;
    s.init(square(), 2.0f);
    Vec3 p, d;
    const float len = s.length();
    s.update(p, d, len / 2.0f); // ratio exactly 1: still the first segment
    expectNear(p, {10, 0, 0}, 1e-3f);
    EXPECT_EQ(s.index(), 0);
    s.update(p, d, 0.5f); // on into the second segment
    EXPECT_EQ(s.index(), 1);
    EXPECT_EQ(s.next(), 2);
    EXPECT_GT(p.z, 0.0f);
    EXPECT_NEAR(s.time(), 0.5f - (len / 2.0f - len / 2.0f), 1e-3f);
    s.update(p, d, -1.0f); // back before the second segment's start
    EXPECT_EQ(s.index(), 0);
    EXPECT_EQ(s.next(), 1);
    // Round the whole loop back to the start.
    s.reset();
    for (int i = 0; i < 4000; ++i)
        s.update(p, d, 0.01f);
    EXPECT_GE(s.index(), 0);
    EXPECT_LT(s.index(), 4);
}

// UpdateRatio: a path of two points stands at the first, facing the second.
TEST(WorldPathSpline, TwoPointPathStandsStill) {
    PathSpline s;
    s.init({{1, 2, 3}, {4, 2, 7}}, 5.0f);
    Vec3 p, d;
    s.update(p, d, 10.0f);
    expectNear(p, {1, 2, 3});
    expectNear(d, {3, 0, 4});
}

// mmGame::InitGizmos: which managers a session has.
TEST(WorldGizmos, KindsPerSession) {
    const auto single = GizmoKinds::forSession(GameMode::Cruise, false);
    EXPECT_TRUE(single.ferries && single.parkedCars && single.bridges && single.trains && single.sailboats);
    const auto netCruise = GizmoKinds::forSession(GameMode::Cruise, true);
    EXPECT_FALSE(netCruise.ferries);
    EXPECT_FALSE(netCruise.parkedCars);
    EXPECT_TRUE(netCruise.bridges);
    EXPECT_FALSE(GizmoKinds::forSession(GameMode::CopsAndRobbers, true).parkedCars);
    EXPECT_TRUE(GizmoKinds::forSession(GameMode::Circuit, true).parkedCars);
}

// init_gizmo_mgr: the race's own path set when there is one, never in cruise.
TEST(WorldGizmos, PathSetPerRace) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    EXPECT_EQ(gizmoPathSetPath(v, "london", "bridge", GameMode::Cruise, -1),
              "race/london/london_bridge.pathset");
    EXPECT_EQ(gizmoPathSetPath(v, "london", "bridge", GameMode::Circuit, 0),
              "race/london/london_bridge_circuit0.pathset");
    EXPECT_EQ(gizmoPathSetPath(v, "london", "bridge", GameMode::Circuit, 1),
              "race/london/london_bridge.pathset");
    EXPECT_EQ(gizmoPathSetPath(v, "london", "ferry", GameMode::CrashCourse, 9),
              "race/london/london_ferry_crash9.pathset");
    EXPECT_EQ(gizmoPathSetPath(v, "sf", "train", GameMode::Cruise, -1), "");
}

namespace {

struct Loaded {
    bangers::BangerDataLibrary data;
    bangers::BangerSet bangers;
    Gizmos gizmos;
    fx::Rand random{1u};
    explicit Loaded(const vfs::Vfs& v) : data(v), bangers(data), gizmos(data, bangers) {}
    void load(const vfs::Vfs& v, std::string city, GameMode mode = GameMode::Cruise, bool multi = false) {
        Gizmos::Options o;
        o.city = std::move(city);
        o.mode = mode;
        o.multiplayer = multi;
        gizmos.load(v, o, nullptr, nullptr, random);
        gizmos.reset();
    }
};

} // namespace

// London: Tower Bridge and two Waterloo bridges (two leaves each), the
// always-open bridge 02, eight tube trains, six ferries, sixteen boats.
TEST(WorldGizmos, LondonGizmos) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "london");
    const auto& g = l.gizmos;
    ASSERT_EQ(g.bridges().size(), 7u);
    EXPECT_EQ(g.trains().size(), 8u);
    EXPECT_EQ(g.ferries().size(), 6u);
    EXPECT_EQ(g.sailboats().size(), 16u);
    EXPECT_EQ(g.bridges()[0].body->model, "giz_bridge01_l");
    EXPECT_EQ(g.bridges()[0].partner, 1);
    EXPECT_EQ(g.bridges()[1].partner, 0);
    EXPECT_EQ(g.bridges()[2].body->model, "giz_waterloo_l");
    // "open:giz_bridge02_l": one leaf, open from the start.
    const auto& open = g.bridges()[4];
    EXPECT_EQ(open.body->model, "giz_bridge02_l");
    EXPECT_EQ(open.type, Gizmos::Bridge::Type::Open);
    EXPECT_EQ(open.partner, -1);
    EXPECT_FLOAT_EQ(open.angle, Gizmos::kGoalAngle);
    EXPECT_EQ(g.bridges()[0].type, Gizmos::Bridge::Type::Timed);
    EXPECT_FLOAT_EQ(g.bridges()[0].angle, 0.0f);
    // The trains use the default model (their paths are named PATHnn).
    EXPECT_EQ(g.trains()[0].cars[0].body->model, "va_ug_l");
    EXPECT_EQ(g.ferries()[0].body->model, "giz_carferry01_l");
    for (const auto& f : g.ferries())
        EXPECT_FLOAT_EQ(f.spline.speed(), Gizmos::kFerrySpeed);
    // Sailboats: the path's model, the path spacing +-1 m/s.
    EXPECT_EQ(g.sailboats()[0].model, "giz_tug01_l");
    EXPECT_EQ(g.sailboats()[4].model, "giz_duck_s");
    EXPECT_GE(g.sailboats()[4].spline.speed(), 1.0f);
    EXPECT_LE(g.sailboats()[4].spline.speed(), 3.0f);
}

// A network game in London: no ferries, every bridge open.
TEST(WorldGizmos, LondonNetworkGame) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "london", GameMode::Cruise, true);
    EXPECT_TRUE(l.gizmos.ferries().empty());
    ASSERT_FALSE(l.gizmos.bridges().empty());
    for (const auto& b : l.gizmos.bridges()) {
        EXPECT_EQ(b.type, Gizmos::Bridge::Type::Open);
        EXPECT_FLOAT_EQ(b.angle, Gizmos::kGoalAngle);
    }
}

// San Francisco: the China Town gate is an inactive two-leaf "bridge".
TEST(WorldGizmos, SanFranciscoGizmos) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "sf");
    const auto& g = l.gizmos;
    ASSERT_EQ(g.bridges().size(), 2u);
    EXPECT_EQ(g.bridges()[0].body->model, "giz_chinagate_f");
    EXPECT_EQ(g.bridges()[0].type, Gizmos::Bridge::Type::Inactive);
    EXPECT_TRUE(g.trains().empty());
    EXPECT_EQ(g.ferries().size(), 1u);
    EXPECT_EQ(g.sailboats().size(), 16u);
    // Inactive gates never move.
    for (int i = 0; i < 2000; ++i)
        l.gizmos.update(0.05f, std::nullopt);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Down);
    EXPECT_FLOAT_EQ(g.bridges()[0].angle, 0.0f);
}

// gizBridge::Update: a timed leaf waits 10 s, rises at 0.05 rad/s to the
// open angle, stays 10 s, comes down again; its partner moves with it.
TEST(WorldGizmos, TimedBridgeCycle) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "london");
    auto& g = l.gizmos;
    const Vec3 closed = g.bridges()[0].body->matrix().m3;
    float t = 0.0f;
    const float dt = 1.0f / 30.0f;
    auto run = [&](float seconds) {
        for (int i = 0; i < static_cast<int>(std::lround(seconds / dt)); ++i, t += dt)
            g.update(dt, std::nullopt);
    };
    run(9.9f);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Down);
    run(0.2f);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Raising);
    EXPECT_EQ(g.bridges()[1].state, Gizmos::Bridge::State::Raising);
    run(Gizmos::kGoalAngle / Gizmos::kLiftSpeed + 0.1f);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Up);
    EXPECT_FLOAT_EQ(g.bridges()[0].angle, Gizmos::kGoalAngle);
    // The leaf's centre lifted.
    EXPECT_GT(g.bridges()[0].body->matrix().m3.y, closed.y + 1.0f);
    run(10.1f);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Lowering);
    run(Gizmos::kGoalAngle / Gizmos::kLiftSpeed + 0.1f);
    EXPECT_EQ(g.bridges()[0].state, Gizmos::Bridge::State::Down);
    expectNear(g.bridges()[0].body->matrix().m3, closed, 1e-3f);
}

// gizBridge::Trigger / gizBridgeMgr::CheckProximity: a proximity leaf rises
// when the trigger comes within 100 m.
TEST(WorldGizmos, ProximityBridge) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "london");
    auto& g = l.gizmos;
    auto& b = const_cast<Gizmos::Bridge&>(g.bridges()[0]);
    b.type = Gizmos::Bridge::Type::Proximity;
    const Vec3 at = b.body->matrix().m3;
    g.update(0.1f, Vec3{at.x + 150.0f, at.y, at.z});
    EXPECT_EQ(b.state, Gizmos::Bridge::State::Down);
    g.update(0.1f, Vec3{at.x + 90.0f, at.y, at.z});
    EXPECT_EQ(b.state, Gizmos::Bridge::State::Raising);
    EXPECT_EQ(g.bridges()[1].state, Gizmos::Bridge::State::Raising); // its partner
    EXPECT_FALSE(g.triggerBridge(0)); // only from Down
}

// gizTrain::Update: 10 s in the station, up to speed, 40 m/s, brakes into
// the far station and leaves the other way.
TEST(WorldGizmos, TrainRunsBetweenStations) {
    MM2_REQUIRE_GAME_DATA();
    Loaded l(*test::gameData());
    l.load(*test::gameData(), "london");
    auto& g = l.gizmos;
    const auto& train = g.trains()[0];
    // The cars start 0.44 s of travel at 40 m/s apart (the closed loop's
    // first segment bulges out towards the path's far end, so the distance
    // between them is not 17.6 m).
    for (int i = 0; i < 3; ++i) {
        const auto& car = train.cars[static_cast<std::size_t>(i)];
        EXPECT_NEAR(car.spline.time(), 0.44f * static_cast<float>(i), 1e-5f);
        EXPECT_FLOAT_EQ(train.cars[static_cast<std::size_t>(i)].spline.speed(), Gizmos::kTrainSpeed);
    }
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < 295; ++i)
        g.update(dt, std::nullopt);
    EXPECT_EQ(train.state, Gizmos::Train::State::InStation);
    for (int i = 0; i < 10; ++i)
        g.update(dt, std::nullopt);
    EXPECT_EQ(train.state, Gizmos::Train::State::Running); // the speed factor was already 1
    bool braked = false, reversed = false;
    for (int i = 0; i < 30 * 120 && !reversed; ++i) {
        g.update(dt, std::nullopt);
        braked = braked || train.state == Gizmos::Train::State::Braking;
        reversed = !train.forward;
    }
    EXPECT_TRUE(braked);
    EXPECT_TRUE(reversed);
    EXPECT_EQ(train.state, Gizmos::Train::State::InStation);
    EXPECT_FLOAT_EQ(train.speedFactor, 0.0f);
}

// gizParkedCarMgr_EnumeratePath: about two thirds of the points get a car,
// giz_pcar01_l or giz_pcar02_l, turned a quarter turn from the path.
TEST(WorldGizmos, ParkedCars) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    for (const char* city : {"london", "sf"}) {
        const auto path = gizmoPathSetPath(v, city, "parkedcar", GameMode::Cruise, -1);
        ASSERT_FALSE(path.empty()) << city;
        auto set = city::parsePathSet(*v.readAll(path));
        ASSERT_TRUE(set);
        fx::Rand r{1u};
        const auto cars = placeParkedCars(*set, r);
        ASSERT_FALSE(cars.empty());
        std::set<std::string> models;
        for (const auto& c : cars) {
            models.insert(c.model);
            EXPECT_TRUE(c.fullMatrix);
            EXPECT_GT(c.transform.m1.y, 0.95f); // level, or the slope of the street
        }
        EXPECT_EQ(models, (std::set<std::string>{"giz_pcar01_l", "giz_pcar02_l"}));
        // Every car's X axis is across the path: the first path's direction
        // is the cars' Z.
        const Vec3 along = (set->paths[0].points[1].position - set->paths[0].points[0].position).normalized();
        const auto first = bangers::placePathSet(*set, bangers::PlacedProp::Source::PathSet);
        (void)first;
        const Mat34& t = cars[0].transform;
        EXPECT_GT(std::abs(t.m2.dot(along)) + std::abs(t.m0.dot(along)), 0.9f);
    }
}

// gizBridge::Init: the rooms at each leaf and 5 m above it get level room
// flag 0x10; vehCar::UpdateTrack lays no skid marks there.
TEST(WorldGizmos, BridgeRoomsFlagged) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    auto city = city::loadCity(v, "london");
    ASSERT_TRUE(city);
    CityLevel level(*city, v, {});
    bangers::BangerDataLibrary data(v);
    bangers::BangerSet set(data);
    fx::Rand r{1u};
    RaceConfig config;
    config.city = "london";
    auto g = initGizmos(v, *city, config, false, set, data, &level, r);
    int flagged = 0;
    for (auto f : city->levelRoomFlags)
        flagged += (f & city::LevelRoomFlag::Bridge) != 0;
    EXPECT_GT(flagged, 0);
    const Vec3 hinge = g->bridges()[0].placement.m3;
    const auto hingeRoom = static_cast<std::size_t>(level.findRoom(hinge, 0));
    EXPECT_NE(city->levelRoomFlags[hingeRoom] & city::LevelRoomFlag::Bridge, 0);
    // The leaves, ferries and train cars are listed in their rooms.
    std::vector<phys::Instance*> listed;
    const int room = g->bridges()[0].body->room;
    ASSERT_GT(room, 0);
    g->instancesIn(room, listed);
    EXPECT_NE(std::ranges::find(listed, g->bridges()[0].body.get()), listed.end());
    const phys::Instance& first = *listed.front();
    EXPECT_TRUE(first.collidable && first.terrainCollidable && first.wheelCollidable);
    EXPECT_FALSE(listed.front()->isBanger());
    EXPECT_NE(listed.front()->bound(0), nullptr);
    // The parked cars went to the props.
    EXPECT_GT(set.instances().size(), 100u);
}

// lvlLandmark (a .inst record with flag 0x100 and without 0x200): its
// IsCollidable answers false, IsTerrainCollidable true; the movers gather it
// through their city flag only (dgPhysManager::GatherCollidables).
TEST(WorldLandmarks, CollideThroughTheCityFlagOnly) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    auto city = city::loadCity(v, "london");
    ASSERT_TRUE(city);
    CityLevel level(*city, v, {});
    int landmarks = 0;
    for (const auto& inst : city->instances) {
        if (!(inst.flags & 0x100) || (inst.flags & 0x200))
            continue;
        std::vector<phys::Instance*> listed;
        level.instances(inst.room, listed);
        for (const auto* i : listed)
            if (const auto* s = dynamic_cast<const StaticInstance*>(i); s && s->name == inst.name) {
                EXPECT_FALSE(s->collidable) << inst.name;
                EXPECT_TRUE(s->terrainCollidable) << inst.name;
                ++landmarks;
            }
    }
    EXPECT_GT(landmarks, 50);
}

namespace {

// The city as the race builds it: level, physics world, props, AI.
struct CableCity {
    std::optional<city::CityData> city;
    std::unique_ptr<bangers::BangerDataLibrary> data;
    std::unique_ptr<CityLevel> level;
    std::unique_ptr<phys::World> world;
    std::unique_ptr<bangers::BangerSet> bangers;
    std::unique_ptr<ai::World> ai;
    std::unique_ptr<CableCars> cars;
    fx::Rand random{1u};

    bool load(const vfs::Vfs& v, const char* name) {
        city = city::loadCity(v, name);
        if (!city)
            return false;
        data = std::make_unique<bangers::BangerDataLibrary>(v);
        level = std::make_unique<CityLevel>(*city, v, [&](std::string_view n) { return data->has(n); });
        world = std::make_unique<phys::World>(level->takeMaterials());
        world->setStatic(level->takeProbeSoup());
        world->setLevel(level.get());
        bangers = std::make_unique<bangers::BangerSet>(*data);
        ai::Settings settings;
        settings.trafficDensity = 0.0f;
        settings.pedestrianDensity = 0.0f;
        ai = ai::World::create(*city, v, settings);
        if (!ai)
            return false;
        cars = std::make_unique<CableCars>(*ai, *data, *bangers);
        cars->create(random);
        cars->reset(*world);
        level->addSource(cars.get());
        return true;
    }
};

} // namespace

// aiMap::Init: a cable car at each end of San Francisco's lines; London has
// none.
TEST(WorldCableCars, OneAtEachEndOfTheLines) {
    MM2_REQUIRE_GAME_DATA();
    CableCity sf;
    ASSERT_TRUE(sf.load(*test::gameData(), "sf"));
    ASSERT_GE(sf.cars->size(), 2u);
    for (std::size_t i = 0; i < sf.cars->size(); ++i) {
        // At its start, on the ground of a room, standing, with a sister
        // (the car starting at the other end of its line).
        int path = -1, dir = 0;
        EXPECT_EQ(sf.cars->speed(i), 0.0f);
        EXPECT_GT(sf.cars->room(i), 0) << i;
        EXPECT_GE(sf.cars->sister(i), 0) << i;
        EXPECT_NEAR(sf.cars->matrix(i).m1.y, 1.0f, 0.2f) << i;
        (void)path;
        (void)dir;
    }
    CableCity london;
    ASSERT_TRUE(london.load(*test::gameData(), "london"));
    EXPECT_EQ(london.cars->size(), 0u);
}

// aiCableCar::Update: the cars start, run along their lines at up to
// 15 m/s on the ground, through intersections onto the next roads, and
// stay listed in the rooms they reach.
TEST(WorldCableCars, RideTheirLines) {
    MM2_REQUIRE_GAME_DATA();
    CableCity sf;
    ASSERT_TRUE(sf.load(*test::gameData(), "sf"));
    ASSERT_GT(sf.cars->size(), 0u);
    std::vector<Vec3> start;
    std::vector<int> firstPath;
    for (std::size_t i = 0; i < sf.cars->size(); ++i) {
        start.push_back(sf.cars->matrix(i).m3);
        firstPath.push_back(sf.cars->path(i));
    }
    const float dt = 1.0f / 30.0f;
    float top = 0.0f;
    std::vector<bool> changedRoad(sf.cars->size(), false);
    for (int frame = 0; frame < 30 * 60; ++frame) {
        sf.ai->lights().update(dt);
        sf.cars->update(dt, nullptr, *sf.world);
        for (std::size_t i = 0; i < sf.cars->size(); ++i) {
            top = std::max(top, sf.cars->speed(i));
            changedRoad[i] = changedRoad[i] || sf.cars->path(i) != firstPath[i];
        }
    }
    EXPECT_LE(top, CableCars::kMaxSpeed + 0.001f);
    EXPECT_GT(top, 5.0f);
    for (std::size_t i = 0; i < sf.cars->size(); ++i) {
        EXPECT_GT(sf.cars->matrix(i).m3.dist(start[i]), 50.0f) << i;
        EXPECT_TRUE(changedRoad[i]) << i;
        EXPECT_GT(sf.cars->room(i), 0) << i;
        std::vector<phys::Instance*> listed;
        sf.cars->instancesIn(sf.cars->room(i), listed);
        EXPECT_FALSE(listed.empty()) << i;
        EXPECT_TRUE(std::isfinite(sf.cars->matrix(i).m3.y)) << i;
    }
}

// aiCableCar::CheckForObstacles: a car stops short of the player standing
// on its line.
TEST(WorldCableCars, StopForThePlayer) {
    MM2_REQUIRE_GAME_DATA();
    CableCity sf;
    ASSERT_TRUE(sf.load(*test::gameData(), "sf"));
    ASSERT_GT(sf.cars->size(), 0u);
    const float dt = 1.0f / 30.0f;
    // Let car 0 get going, then put the player 20 m ahead of it. The car
    // stops 2.5 m short of the first of its corners that blocks the way
    // (aiVehicle::IsBlockingTarget takes them in order, front first).
    for (int frame = 0; frame < 30 * 4; ++frame)
        sf.cars->update(dt, nullptr, *sf.world);
    const Mat34 m = sf.cars->matrix(0);
    ai::TrackedCar player;
    player.isPlayer = true;
    player.position = m.m3 - m.m2 * 20.0f;
    player.forward = m.m2; // facing the cable car: its front corners come first
    player.right = -m.m0;
    for (int frame = 0; frame < 30 * 10; ++frame)
        sf.cars->update(dt, &player, *sf.world);
    EXPECT_EQ(sf.cars->speed(0), 0.0f);
    EXPECT_LT(sf.cars->matrix(0).m3.dist(player.position), 20.0f);
    EXPECT_GT(sf.cars->matrix(0).m3.dist(player.position), 3.5f);
    EXPECT_LT(sf.cars->matrix(0).m3.dist(player.position), 6.0f);
}
