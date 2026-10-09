// The cruise start against MM2's own code (MM2Recomp, build 3393):
// mmGame::RespawnXYZ, mmSingleRoam / mmMultiRoam / mmMultiCR's
// InitOtherPlayers and Reset, and the draws aiMap::Reset makes from the one
// random stream before them. See docs/parity/round3/cruise-spawn.md.

#include "TestData.h"
#include "ai/Random.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "city/RoomInfo.h"
#include "city/RoomLocator.h"
#include "city/SdlDraw.h"
#include "game/PlayerVehicle.h"
#include "game/RaceConfig.h"
#include "game/Strings.h"
#include "game/session/RaceSetup.h"
#include "game/session/Session.h"

#include <gtest/gtest.h>

#include <memory>
#include <set>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

// Sets mmGame's respawn counter for a test and puts it back afterwards.
struct CounterScope {
    int saved;
    explicit CounterScope(int value) : saved(respawnCounter()) { respawnCounter() = value; }
    ~CounterScope() { respawnCounter() = saved; }
};

// A city of `n` intersections in rooms 1..n (intersection i in room i), one
// road each, intersection i at (10 i, 0, 0).
city::CityData syntheticCity(int n) {
    city::CityData c;
    c.aiMap.emplace();
    c.levelRoomFlags.assign(static_cast<std::size_t>(n + 1), 0);
    for (int i = 0; i < n; ++i) {
        city::AiIntersection x;
        x.id = static_cast<std::uint16_t>(i);
        x.room = static_cast<std::uint16_t>(i + 1);
        x.center = {10.0f * static_cast<float>(i), 0.0f, 0.0f};
        x.paths = {static_cast<std::uint32_t>(i)};
        c.aiMap->intersections.push_back(x);
        city::AiPath p;
        p.id = static_cast<std::uint16_t>(i);
        c.aiMap->paths.push_back(p);
    }
    return c;
}

RoomLookup syntheticRooms() {
    return [](const Vec3& p) { return static_cast<int>(p.x / 10.0f + 0.5f) + 1; };
}

// RespawnXYZ's draw: irand() % (n - 1) + 1.
int draw(ai::Random& r, int n) { return r.irand() % (n - 1) + 1; }

struct Retail {
    city::CityData london, sf;
};

Retail* retail() {
    static std::unique_ptr<Retail> r = []() -> std::unique_ptr<Retail> {
        if (!test::gameData())
            return nullptr;
        auto london = city::loadCity(*test::gameData(), "london");
        auto sf = city::loadCity(*test::gameData(), "sf");
        if (!london || !sf)
            return nullptr;
        auto out = std::make_unique<Retail>();
        out->london = std::move(*london);
        out->sf = std::move(*sf);
        return out;
    }();
    return r.get();
}

// The number of irand() calls from seed 1 (ResetRandomSeed) to `seed`.
int drawsFromOne(std::uint32_t seed) {
    ai::Random r;
    int n = 0;
    while (r.state() != seed && n < 100000) {
        r.irand();
        ++n;
    }
    return n;
}

// mmGame::Init for a single-player cruise in `city` with the menu's cruise
// settings (RaceMenuBase::SetStateRace: traffic 0.5, pedestrians 0.25) in
// vpbug: the seed aiMap::Reset leaves with the car at mmSingleRoam's
// InitGameObjects place, mmGame's (0, 10, 0).
std::uint32_t cruiseResetSeed(const city::CityData& city) {
    RaceConfig cfg;
    cfg.mode = GameMode::Cruise;
    applyRaceTableDefaults(cfg, nullptr);
    ai::Settings settings;
    settings.trafficDensity = cfg.trafficDensity;
    settings.pedestrianDensity = cfg.pedestrianDensity;
    std::string error;
    const city::AiMapConfig* config = city.cruise ? &*city.cruise : nullptr;
    auto world = ai::World::create(city, *test::gameData(), settings, config, &error);
    EXPECT_TRUE(world) << error;
    auto car = SimVehicle::loadPlayer(*test::gameData(), cfg.vehicle, &error, true);
    EXPECT_TRUE(car) << error;
    if (!world || !car)
        return 0;
    car->setResetPos({0.0f, 10.0f, 0.0f}, 0.0f);
    car->reset();
    return world->globalSeedAfterReset(car->sim().resetPos());
}

} // namespace

// mmGame::RespawnXYZ: each pick is the last of (counter + 1) draws
// irand() % (n - 1) + 1 (never intersection 0), repeated until one fits; the
// start is its centre 2 m up, angle 0.
TEST(ParityCruiseSpawn, RespawnDrawsCounterPlusOneAndRetries) {
    auto city = syntheticCity(6);
    const RoomLookup rooms = syntheticRooms();
    for (int draws : {1, 2, 5}) {
        for (std::uint32_t seed : {1u, 7u, 12345u}) {
            ai::Random expect;
            expect.seed(seed);
            int index = 0;
            for (int k = 0; k < draws; ++k)
                index = draw(expect, 6);
            std::uint32_t stream = seed;
            const auto pick = respawnXYZ(city, rooms, {true, true}, stream, draws);
            ASSERT_TRUE(pick);
            EXPECT_EQ(pick->intersection, index);
            EXPECT_EQ(pick->position, Vec3(10.0f * static_cast<float>(index), 2.0f, 0.0f));
            EXPECT_EQ(pick->angle, 0.0f);
            EXPECT_EQ(stream, expect.state());
        }
    }
}

// The rules: rooms flagged 0x24 always fail, 0x0A with the second argument;
// a freeway road (aiPath flag 0x4) with the first, an alley (0x2) with the
// second. A failed pick draws again.
TEST(ParityCruiseSpawn, RespawnRejectsRoomsAndRoads) {
    auto city = syntheticCity(3);
    const RoomLookup rooms = syntheticRooms();
    auto pickWith = [&](RespawnRules rules) {
        std::uint32_t stream = 1;
        return respawnXYZ(city, rooms, rules, stream, 1)->intersection;
    };
    // Seed 1 draws 2 first (irand: 41).
    ai::Random r;
    std::vector<int> seq;
    for (int k = 0; k < 5; ++k)
        seq.push_back(draw(r, 3));
    ASSERT_EQ(seq.front(), 2);
    const int first = seq[0];
    int second = -1;
    for (int v : seq)
        if (v != first) {
            second = v;
            break;
        }
    ASSERT_NE(second, -1);
    EXPECT_EQ(pickWith({}), first);
    for (std::uint16_t flags : {city::LevelRoomFlag::WaterOfDeath, city::LevelRoomFlag::TerrainInstance}) {
        city.levelRoomFlags[static_cast<std::size_t>(first) + 1] = flags;
        EXPECT_EQ(pickWith({}), second) << flags;
    }
    for (std::uint16_t flags : {city::LevelRoomFlag::Subterranean, city::LevelRoomFlag::Covered}) {
        city.levelRoomFlags[static_cast<std::size_t>(first) + 1] = flags;
        EXPECT_EQ(pickWith({}), first) << flags;
        EXPECT_EQ(pickWith({false, true}), second) << flags;
    }
    // Bridge (0x10), warp (0x40) and open road (0x01) rooms are fine.
    city.levelRoomFlags[static_cast<std::size_t>(first) + 1] = 0x51;
    EXPECT_EQ(pickWith({true, true}), first);
    city.levelRoomFlags[static_cast<std::size_t>(first) + 1] = 0;
    city.aiMap->paths[static_cast<std::size_t>(first)].flags = 0x4;
    EXPECT_EQ(pickWith({false, true}), first);
    EXPECT_EQ(pickWith({true, false}), second);
    city.aiMap->paths[static_cast<std::size_t>(first)].flags = 0x2;
    EXPECT_EQ(pickWith({true, false}), first);
    EXPECT_EQ(pickWith({false, true}), second);
    // Every intersection but the first fails: MM2 would never return.
    city.aiMap->paths[1].flags = city.aiMap->paths[2].flags = 0x6;
    std::uint32_t stream = 1;
    EXPECT_FALSE(respawnXYZ(city, rooms, {true, true}, stream, 1));
    auto tiny = syntheticCity(1);
    EXPECT_FALSE(respawnXYZ(tiny, rooms, {}, stream, 1));
}

// mmMultiRoam / mmMultiCR: Reset and then InitNetworkPlayers each draw from
// the player's own seed and count one more seeded call; the start is the
// second pick. The count stays for the run, so a later single-player start
// draws more numbers a pick.
TEST(ParityCruiseSpawn, MultiplayerStartIsTheSecondSeededPick) {
    CounterScope scope(0);
    auto city = syntheticCity(9);
    const RoomLookup rooms = syntheticRooms();
    const std::uint32_t player = 1234;
    std::uint32_t stream = player;
    const auto second = respawnXYZ(city, rooms, {true, true}, stream, 2);
    const auto pick = cruiseStart(city, rooms, true, 999, player);
    ASSERT_TRUE(pick && second);
    EXPECT_EQ(pick->intersection, second->intersection);
    EXPECT_EQ(respawnCounter(), 2);
    // Single player: the global seed, 3 draws a pick now, the counter kept.
    stream = 77;
    const auto three = respawnXYZ(city, rooms, {true, true}, stream, 3);
    const auto single = cruiseStart(city, rooms, false, 77, player);
    ASSERT_TRUE(single && three);
    EXPECT_EQ(single->intersection, three->intersection);
    EXPECT_EQ(respawnCounter(), 2);
    // mod 100.
    respawnCounter() = 99;
    cruiseStart(city, rooms, true, 0, player);
    EXPECT_EQ(respawnCounter(), 1);
}

// CreateRoadMap moves the centre of every intersection a shortcut road of
// <city>_sup.bai joins to its room's bound-sphere centre
// (cityLevel::GetBoundSphere); RespawnXYZ starts there.
TEST(ParityCruiseSpawn, ShortcutEndsTakeTheRoomCentre) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    for (const auto* city : {&retail()->london, &retail()->sf}) {
        const auto& map = *city->aiMap;
        ASSERT_GT(map.numShortcuts, 0u);
        std::set<std::uint32_t> ends;
        for (std::size_t i = map.paths.size() - map.numShortcuts; i < map.paths.size(); ++i)
            for (const auto& e : map.paths[i].ends)
                ends.insert(e.intersection);
        for (const auto i : ends) {
            const auto& x = map.intersections[i];
            Vec3 centre;
            float radius = 0.0f;
            city::sdlRoomBoundSphere(city->psdl, x.room, centre, radius);
            EXPECT_EQ(x.center, centre) << city->info.mapName << " " << i;
        }
    }
}

// RespawnXYZ's room is cityLevel::FindRoomId of the intersection's centre,
// not the room the .bai gives it: San Francisco's intersection 187 lists room
// 1078 (an open road) but its centre lies in room 265, a terrain instance's
// (0x20), so it is never a start.
TEST(ParityCruiseSpawn, RespawnRoomIsTheCentresRoom) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    const auto& sf = retail()->sf;
    const city::RoomLocator rooms(sf.psdl, "sf");
    const auto& x = sf.aiMap->intersections[187];
    EXPECT_EQ(x.room, 1078);
    EXPECT_EQ(sf.levelRoomFlags[1078] & 0x2E, 0);
    ASSERT_EQ(rooms.find(x.center), 265);
    EXPECT_EQ(sf.levelRoomFlags[265], city::LevelRoomFlag::TerrainInstance);
    const RoomLookup findRoom = [&](const Vec3& p) { return rooms.find(p); };
    std::uint32_t stream = 1;
    for (int i = 0; i < 2000; ++i)
        EXPECT_NE(respawnXYZ(sf, findRoom, {true, true}, stream, 1)->intersection, 187);
}

// aiMap::Reset in mmGame::Init (ResetRandomSeed, then AdjustAmbients and
// AdjustPedestrians for the room of the car's reset position, (0, 10, 0) +
// its CG): London's room 65 holds 28 ambient roads and 13 pedestrian roads,
// San Francisco's room 265 three ambient roads and none for pedestrians. With
// the cruise menu's densities the ambient cars draw 73 and 21 numbers (one
// link choice each) and the 25 pedestrians 4 each.
TEST(ParityCruiseSpawn, ResetDrawsBeforeTheStart) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    const city::RoomLocator london(retail()->london.psdl, "london"), sf(retail()->sf.psdl, "sf");
    EXPECT_EQ(london.find({0.0f, 9.9f, 0.0f}), 65);
    EXPECT_EQ(sf.find({0.0f, 9.9f, 0.0f}), 265);
    EXPECT_EQ(drawsFromOne(cruiseResetSeed(retail()->london)), 73 + 25 * 4);
    EXPECT_EQ(drawsFromOne(cruiseResetSeed(retail()->sf)), 21);
}

// The default single-player cruise starts (MM2 picks the same one every time
// in a city until a network game changes the respawn counter):
// - London: intersection 19, the road along the north side of Marble Arch's
//   island in front of the Underground station;
// - San Francisco: intersection 121, on the Embarcadero, about 160 m south
//   of the Ferry Building.
TEST(ParityCruiseSpawn, LondonAndSanFranciscoStarts) {
    MM2_REQUIRE_GAME_DATA();
    ASSERT_TRUE(retail());
    CounterScope scope(0);
    struct Expect {
        const city::CityData* city;
        int intersection;
        Vec3 centre;
    };
    for (const Expect& e : {Expect{&retail()->london, 19, {-542.529f, 4.934f, -676.292f}},
                            Expect{&retail()->sf, 121, {40.306f, -0.082f, 153.295f}}}) {
        const city::RoomLocator rooms(e.city->psdl, e.city->info.mapName);
        const RoomLookup findRoom = [&](const Vec3& p) { return rooms.find(p); };
        const std::uint32_t seed = cruiseResetSeed(*e.city);
        const auto pick = cruiseStart(*e.city, findRoom, false, seed, 1);
        ASSERT_TRUE(pick) << e.city->info.mapName;
        EXPECT_EQ(pick->intersection, e.intersection) << e.city->info.mapName;
        const Vec3& centre = e.city->aiMap->intersections[static_cast<std::size_t>(e.intersection)].center;
        EXPECT_NEAR(centre.x, e.centre.x, 1e-3f);
        EXPECT_NEAR(centre.y, e.centre.y, 1e-3f);
        EXPECT_NEAR(centre.z, e.centre.z, 1e-3f);
        EXPECT_EQ(pick->position, centre + Vec3(0.0f, 2.0f, 0.0f));
        EXPECT_EQ(pick->angle, 0.0f);

        // The session: (0, 10, 0) until InitOtherPlayers, then the start,
        // which the water respawn and restarts keep.
        RaceConfig cfg;
        cfg.mode = GameMode::Cruise;
        cfg.city = e.city->info.mapName;
        applyRaceTableDefaults(cfg, nullptr);
        Strings strings;
        std::string error;
        auto s = Session::create(cfg, *e.city, *test::gameData(), strings, &error);
        ASSERT_TRUE(s) << error;
        EXPECT_TRUE(s->setup().respawnStart);
        EXPECT_EQ(s->setup().playerDrop, StartDrop::None);
        EXPECT_EQ(s->playerSpawn().m3, Vec3(0.0f, 10.0f, 0.0f));
        const auto placed = s->placeRespawnStart(*e.city, seed, findRoom);
        ASSERT_TRUE(placed);
        EXPECT_EQ(placed->intersection, e.intersection);
        EXPECT_EQ(s->playerSpawn().m3, pick->position);
        EXPECT_EQ(s->setup().playerPlace.angle, 0.0f);
        EXPECT_EQ(s->playerSpawn().m2, Vec3(0.0f, 0.0f, 1.0f)); // facing -Z
        s->start();
        s->restart();
        EXPECT_EQ(s->playerSpawn().m3, pick->position);
    }
}

// The car on the start: vehCarSim::SetResetPos adds the CG unrotated, the
// reset angle 0 turns nothing, and vehCar::Reset puts the body there with
// the model at body + R * CG; cruise does not settle it on the ground.
TEST(ParityCruiseSpawn, TheCarStandsOnTheStart) {
    MM2_REQUIRE_GAME_DATA();
    std::string error;
    auto car = SimVehicle::loadPlayer(*test::gameData(), "vpbug", &error, true);
    ASSERT_TRUE(car) << error;
    const Vec3 start{-542.529f, 6.934f, -676.292f};
    car->setResetPos(start, 0.0f);
    car->reset();
    const Vec3 cg = car->sim().centerOfGravity;
    EXPECT_EQ(car->sim().resetPos(), Vec3(cg.x + start.x, cg.y + start.y, cg.z + start.z));
    const auto& body = car->sim().body.ics.matrix;
    EXPECT_EQ(body.m3, car->sim().resetPos());
    EXPECT_EQ(body.m0, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(body.m2, Vec3(0.0f, 0.0f, 1.0f));
    const Mat34 model = car->sim().modelMatrix();
    EXPECT_NEAR(model.m3.y, body.m3.y + cg.y, 1e-4f);
}
