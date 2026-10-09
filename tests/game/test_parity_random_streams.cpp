// Round 3 parity checks for MM2's one global random stream (gRandSeed)
// through a race's set-up, on retail data: cityLevel::Load's street props
// (seed 1 before every road) leave the stream as the last road's walk does;
// mmPlayer::Init's vehCar::Init, mmGame::InitGizmos, the racers' cars,
// aiMap::Init's ambient pool and pedestrians and the cable cars draw from it
// in that order; aiMap::Reset sets it to 1 and places the traffic, then the
// pedestrians. See docs/parity/round3/random-streams.md.
#include "TestData.h"

#include "ai/Random.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/CityLevel.h"
#include "game/RaceConfig.h"
#include "game/VehicleRenderer.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/world/CableCars.h"
#include "game/world/Gizmos.h"
#include "game/PlayerVehicle.h"
#include "city/RoomLocator.h"
#include "game/session/RaceSetup.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::game;

namespace {

std::uint32_t advanced(std::uint32_t state, int draws) {
    ai::Random r(state);
    r.discard(draws);
    return r.state();
}

// mmGame::Init's set-up of a single-player cruise in `cityName` (no racers),
// in the order RaceScreen makes it.
struct RaceSetUp {
    std::optional<city::CityData> city;
    std::unique_ptr<bangers::BangerDataLibrary> data;
    std::unique_ptr<CityLevel> level;
    std::unique_ptr<phys::World> physics;
    std::unique_ptr<bangers::BangerSet> bangers;
    std::unique_ptr<world::Gizmos> gizmos;
    std::unique_ptr<ai::World> ai;
    std::unique_ptr<world::CableCars> cableCars;
    ai::Random random{1u};
    std::uint32_t afterProps = 0, playerFlares = 0, afterPlayer = 0, afterGizmos = 0, afterAi = 0,
                  afterCableCars = 0;
    std::size_t parkedCars = 0;

    bool load(const vfs::Vfs& v, const char* cityName, float traffic = 1.0f, float peds = 1.0f) {
        city = city::loadCity(v, cityName);
        if (!city)
            return false;
        data = std::make_unique<bangers::BangerDataLibrary>(v);
        level = std::make_unique<CityLevel>(*city, v, [&](std::string_view n) { return data->has(n); });
        physics = std::make_unique<phys::World>(level->takeMaterials());
        physics->setStatic(level->takeProbeSoup());
        physics->setLevel(level.get());
        // cityLevel::Load.
        bangers = std::make_unique<bangers::BangerSet>(*data);
        std::uint32_t state = 1;
        bangers->add(bangers::placeCityProps(*city, v, *data, "roam", &state));
        afterProps = state;
        random.seed(state);
        // mmPlayer::Init.
        playerFlares = takeVehCarInitDraws(random);
        afterPlayer = random.state();
        // mmGame::InitGizmos.
        RaceConfig config;
        config.city = cityName;
        const std::size_t before = bangers->instances().size();
        gizmos = world::initGizmos(v, *city, config, false, *bangers, *data, level.get(), random);
        parkedCars = bangers->instances().size() - before;
        afterGizmos = random.state();
        // aiMap::Init: no racers in cruise; the ambient pool and the
        // pedestrians at full density, then the cable cars.
        ai::Settings settings;
        settings.trafficDensity = traffic;
        settings.pedestrianDensity = peds;
        settings.random = &random;
        ai = ai::World::create(*city, v, settings);
        if (!ai)
            return false;
        afterAi = random.state();
        cableCars = std::make_unique<world::CableCars>(*ai, *data, *bangers);
        cableCars->create(random);
        afterCableCars = random.state();
        return true;
    }

    // aiMap::Reset and the first step with the player at the intersection
    // with the most lights (a fixed stand-in for the cruise start, which
    // mmGame::RespawnXYZ draws from the same stream).
    void start() {
        ai->reset();
        const auto& net = ai->network();
        std::size_t busiest = 0;
        for (std::size_t i = 0; i < net.intersections().size(); ++i)
            if (net.intersections()[i].lights.size() > net.intersections()[busiest].lights.size())
                busiest = i;
        ai->step(ai::PlayerCar::at(net.intersections()[busiest].centre, {}));
    }
};

} // namespace

// The stream after cityLevel::Load is the last road's walk from seed 1 (or
// 1 when that road has no props): a PSDL holding only the last road leaves
// it the same. Walking the roads without sidewalks too places nothing more
// (the cities' 10,184 street props).
TEST(ParityRandomStreamsRetail, StreetPropsLeaveTheLastRoadsState) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    struct Expected {
        const char* city;
        std::size_t props;
        std::uint32_t state;
    };
    for (const auto& [name, count, expected] :
         {Expected{"london", 4835, 1u}, Expected{"sf", 5349, 2380015889u}}) {
        const auto city = city::loadCity(v, name);
        ASSERT_TRUE(city) << name;
        const std::string dir = std::string("city/") + name + "/";
        const auto defBytes = v.readAll(dir + "propdefs.csv");
        const auto ruleBytes = v.readAll(dir + "proprules.csv");
        ASSERT_TRUE(defBytes && ruleBytes) << name;
        auto text = [](const std::vector<std::byte>& b) {
            return std::string_view(reinterpret_cast<const char*>(b.data()), b.size());
        };
        const auto defs = bangers::parsePropDefs(text(*defBytes));
        const auto rules = bangers::parsePropRules(text(*ruleBytes));
        std::uint32_t state = 0;
        const auto props = bangers::placeStreetProps(city->psdl, defs, rules, &state);
        EXPECT_EQ(props.size(), count) << name;
        EXPECT_EQ(state, expected) << name;
        city::Psdl last = city->psdl;
        last.roads = {city->psdl.roads.back()};
        std::uint32_t lastState = 0;
        bangers::placeStreetProps(last, defs, rules, &lastState);
        EXPECT_EQ(state, lastState) << name;
    }
}

// London cruise: the stream through the set-up, and the traffic and
// pedestrians it decides.
TEST(ParityRandomStreamsRetail, LondonCruiseSetUp) {
    MM2_REQUIRE_GAME_DATA();
    RaceSetUp s;
    ASSERT_TRUE(s.load(*test::gameData(), "london"));
    // London's last road has no props: the player's car draws from 1.
    EXPECT_EQ(s.afterProps, 1u);
    EXPECT_EQ(s.playerFlares, 1u);
    EXPECT_EQ(s.afterPlayer, advanced(1u, kVehCarInitDraws));
    // Sixteen sailboats (paint, speed), six ferries (speed), 488 parked
    // cars (two draws each, one for each point left empty).
    EXPECT_EQ(s.gizmos->sailboats().size(), 16u);
    EXPECT_EQ(s.gizmos->ferries().size(), 6u);
    EXPECT_EQ(s.parkedCars, 488u);
    EXPECT_EQ(s.afterGizmos, 1960625569u);
    // aiMap::Init: 300 ambient cars (two constructor draws each, then four
    // each), 100 pedestrians (London's [Ped Pool] is commented out; two
    // draws each), no cable cars.
    EXPECT_EQ(s.afterAi, advanced(s.afterGizmos, 6 * ai::kAmbientPoolSize + 2 * 100));
    EXPECT_EQ(s.afterCableCars, s.afterAi);

    s.start();
    const auto& cars = s.ai->cars();
    ASSERT_GE(cars.size(), 3u);
    for (const auto& c : cars) {
        // Car i's paint is the third of its four set-up draws.
        ai::Random r(advanced(s.afterGizmos, 2 * ai::kAmbientPoolSize + 4 * c.id + 2));
        EXPECT_FLOAT_EQ(c.paint, r.frand()) << c.id;
    }
    struct Car {
        int id;
        const char* model;
        float paint;
    };
    const Car expectedCars[] = {{198, "va_eurocargo_l", 0.255981445f},
                                {199, "va_eurocargo_l", 0.557220459f},
                                {200, "va_2sitersport_l", 0.655487061f}};
    for (std::size_t i = 0; i < std::size(expectedCars); ++i) {
        EXPECT_EQ(cars[i].id, expectedCars[i].id);
        EXPECT_EQ(cars[i].model, expectedCars[i].model);
        EXPECT_FLOAT_EQ(cars[i].paint, expectedCars[i].paint);
    }
    const auto& peds = s.ai->peds();
    ASSERT_GE(peds.size(), 3u);
    struct Ped {
        int id;
        const char* type;
        int variant;
    };
    const Ped expectedPeds[] = {{0, "pedmodel_man", 37}, {1, "pedmodel_woman", 4}, {2, "pedmodel_man", 32}};
    for (std::size_t i = 0; i < std::size(expectedPeds); ++i) {
        EXPECT_EQ(peds[i].id, expectedPeds[i].id);
        EXPECT_EQ(peds[i].typeName, expectedPeds[i].type);
        EXPECT_EQ(peds[i].variant, expectedPeds[i].variant);
    }

    // A restart (aiMap::Reset from mmGame::Reset) places them all again
    // exactly as at the start.
    std::vector<std::pair<int, Vec3>> first;
    for (const auto& c : cars)
        first.emplace_back(c.id, c.transform.m3);
    std::vector<std::pair<int, Vec3>> firstPeds;
    for (const auto& p : peds)
        firstPeds.emplace_back(p.id, p.transform.m3);
    for (int i = 0; i < 300; ++i)
        s.ai->step(ai::PlayerCar::at(s.ai->network().intersections()[0].centre, {}));
    s.start();
    ASSERT_EQ(s.ai->cars().size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(s.ai->cars()[i].id, first[i].first);
        EXPECT_EQ(s.ai->cars()[i].transform.m3, first[i].second);
    }
    ASSERT_EQ(s.ai->peds().size(), firstPeds.size());
    for (std::size_t i = 0; i < firstPeds.size(); ++i) {
        EXPECT_EQ(s.ai->peds()[i].id, firstPeds[i].first);
        EXPECT_EQ(s.ai->peds()[i].transform.m3, firstPeds[i].second);
    }
}

// San Francisco: the street props leave the stream where its last road's
// walk does; the cable cars draw twice each (aiRailSet's constructor, then
// aiCableCar::Init) after the pedestrians.
TEST(ParityRandomStreamsRetail, SanFranciscoCableCarsDrawAfterThePedestrians) {
    MM2_REQUIRE_GAME_DATA();
    RaceSetUp s;
    ASSERT_TRUE(s.load(*test::gameData(), "sf"));
    EXPECT_EQ(s.afterProps, 2380015889u);
    EXPECT_EQ(s.afterPlayer, advanced(s.afterProps, kVehCarInitDraws));
    // The parked cars from there (90 from seed 1, as OpenMM2 had them).
    EXPECT_EQ(s.parkedCars, 102u);
    const int cableCars = static_cast<int>(s.cableCars->size());
    ASSERT_GT(cableCars, 0);
    EXPECT_EQ(s.afterCableCars, advanced(s.afterAi, 2 * cableCars));
}

// mmGame::RespawnXYZ draws the cruise start from the stream as the first
// aiMap::Reset left it, and that reset starts with ResetRandomSeed: the
// set-up's draws before it (props, the player's car, gizmos, aiMap::Init) do
// not reach the start. With the cruise menu's densities (traffic 0.5,
// pedestrians 0.25) and the car at mmGame's (0, 10, 0), the shared stream
// gives the cruise-spawn record's starts, and the same stream state as a
// world whose stream began anywhere else.
TEST(ParityRandomStreamsRetail, CruiseStartIgnoresTheDrawsBeforeTheReset) {
    MM2_REQUIRE_GAME_DATA();
    const int saved = session::respawnCounter();
    session::respawnCounter() = 0;
    struct Expected {
        const char* city;
        int intersection;
    };
    for (const auto& [name, intersection] : {Expected{"london", 19}, Expected{"sf", 121}}) {
        RaceSetUp s;
        ASSERT_TRUE(s.load(*test::gameData(), name, 0.5f, 0.25f)) << name;
        std::string error;
        auto car = SimVehicle::loadPlayer(*test::gameData(), "vpbug", &error, true);
        ASSERT_TRUE(car) << error;
        car->setResetPos({0.0f, 10.0f, 0.0f}, 0.0f);
        car->reset();
        s.ai->resetAndPopulate(car->sim().resetPos());
        const std::uint32_t seed = s.random.state();
        // The same world on a stream that began elsewhere.
        ai::Random other(0xBADC0DEu);
        ai::Settings settings;
        settings.trafficDensity = 0.5f;
        settings.pedestrianDensity = 0.25f;
        settings.random = &other;
        auto world = ai::World::create(*s.city, *test::gameData(), settings);
        ASSERT_TRUE(world);
        world->resetAndPopulate(car->sim().resetPos());
        EXPECT_EQ(other.state(), seed) << name;
        const city::RoomLocator rooms(s.city->psdl, s.city->info.mapName);
        const auto pick = session::cruiseStart(
            *s.city, [&](const Vec3& p) { return rooms.find(p); }, false, seed, 1);
        ASSERT_TRUE(pick) << name;
        EXPECT_EQ(pick->intersection, intersection) << name;
    }
    session::respawnCounter() = saved;
}
