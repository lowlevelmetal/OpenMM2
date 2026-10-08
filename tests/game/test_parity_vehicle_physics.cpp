// Parity checks for the reverse audit of the vehicle-physics subsystem
// (docs/parity/mm2/vehicle-physics.md): where and how MM2 places its cars.

#include "TestData.h"
#include "city/CityData.h"
#include "game/CityLevel.h"
#include "game/PlayerVehicle.h"
#include "game/session/RaceSetup.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/VehicleGeometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>

using namespace mm2;

namespace {

struct Level {
    std::optional<city::CityData> city;
    std::unique_ptr<game::CityLevel> level;
    std::unique_ptr<phys::World> world;

    static std::unique_ptr<Level> load(const vfs::Vfs& vfs, const char* name) {
        auto l = std::make_unique<Level>();
        l->city = city::loadCity(vfs, name);
        if (!l->city)
            return nullptr;
        l->level = std::make_unique<game::CityLevel>(*l->city, vfs, [](std::string_view) { return false; });
        l->world = std::make_unique<phys::World>(l->level->takeMaterials());
        l->world->setStatic(l->level->takeProbeSoup());
        l->world->setLevel(l->level.get());
        return l;
    }
};

game::GameMode gameMode(city::RaceMode m) {
    switch (m) {
    case city::RaceMode::Blitz: return game::GameMode::Blitz;
    case city::RaceMode::Circuit: return game::GameMode::Circuit;
    case city::RaceMode::Checkpoint: return game::GameMode::Checkpoint;
    case city::RaceMode::CrashCourse: return game::GameMode::CrashCourse;
    }
    return game::GameMode::Cruise;
}

} // namespace

// vehCarSim::Init ends with SetResetPos(origin) and Reset; SetResetPos adds
// CenterOfGravity and vehCarSim::Reset turns the body about Y by the reset
// rotation, keeping the position.
TEST(VehiclePhysicsParity, ResetPositionIsTheStartPlusCenterOfGravity) {
    phys::CarSimParams p;
    p.centerOfGravity = {0.0f, -0.1f, 0.2f};
    phys::CarSim car;
    car.init(p, phys::VehicleGeometry::placeholder());
    EXPECT_EQ(car.resetPos(), p.centerOfGravity);
    EXPECT_EQ(car.body.ics.matrix.m3, p.centerOfGravity);

    const Vec3 start{100.0f, 5.0f, -40.0f};
    car.setResetPos(start);
    car.resetRotation = 1.0f;
    car.reset();
    EXPECT_EQ(car.body.ics.matrix.m3, start + p.centerOfGravity);
    const Mat34 turned = Mat34::rotationY(1.0f);
    EXPECT_NEAR((car.body.ics.matrix.m2 - turned.m2).mag(), 0.0f, 1e-6f);
    // The model origin is the body plus R * CenterOfGravity.
    const Vec3 origin = start + p.centerOfGravity + turned.transformDir(p.centerOfGravity);
    EXPECT_NEAR((car.modelMatrix().m3 - origin).mag(), 0.0f, 1e-5f);

    // A reset (vehCar::Reset) goes back there after the car has moved.
    Mat34 elsewhere = Mat34::rotationY(2.0f);
    elsewhere.m3 = {0.0f, 50.0f, 0.0f};
    car.reset(elsewhere);
    car.reset();
    EXPECT_EQ(car.body.ics.matrix.m3, start + p.centerOfGravity);
}

// mmSingleCircuit / mmGameMulti::HitWaterHandler put the reset position back
// through SetResetPos, which adds CenterOfGravity again: after a respawn at a
// checkpoint, a restart starts the car one CenterOfGravity further on.
TEST(VehiclePhysicsParity, CheckpointRespawnShiftsTheStartByCenterOfGravity) {
    MM2_REQUIRE_GAME_DATA();
    std::string error;
    auto car = game::SimVehicle::load(*test::gameData(), "vpbus", &error);
    ASSERT_TRUE(car) << error;
    const Vec3 cg = car->sim().centerOfGravity;
    ASSERT_NE(cg, Vec3{});
    const Vec3 start{10.0f, 1.0f, 20.0f};
    car->setResetPos(start, 0.5f);
    car->reset();
    Mat34 checkpoint = Mat34::rotationY(-2.0f);
    checkpoint.m3 = {300.0f, 4.0f, -80.0f};
    car->respawnAt(checkpoint);
    EXPECT_NEAR((car->sim().body.ics.matrix.m3 - (checkpoint.m3 + cg)).mag(), 0.0f, 1e-4f);
    EXPECT_FLOAT_EQ(car->sim().resetRotation, 0.5f);
    EXPECT_EQ(car->sim().resetPos(), cg + (cg + start));
    car->reset();
    EXPECT_EQ(car->sim().body.ics.matrix.m3, cg + (cg + start));
}

// Every single-player start (the player's first waypoint, the racers' first
// route points, the police posts) is first used as the race data gives it
// (the modes' InitGameObjects, aiRouteRacer::Init, aiPoliceOfficer::Reset);
// then mmGame::InitOtherPlayers and CollideAIOpponents move the player's
// and the racers' reset positions 0.9 m above the road under them (from the
// body's centre and the model origin). The cars placed so settle on the
// road.
TEST(VehiclePhysicsParity, RaceStartsSettleOnTheRoad) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    const bool verbose = std::getenv("OPENMM2_DIAG") != nullptr;
    for (const char* name : {"sf", "london"}) {
        auto l = Level::load(vfs, name);
        ASSERT_TRUE(l);
        std::map<std::string, std::unique_ptr<game::SimVehicle>> cars;
        auto heightAboveGround = [&](const Vec3& p, float* out) {
            phys::RayHit hit;
            if (!l->world->wheelProbe(p + Vec3{0, 5, 0}, p - Vec3{0, 30, 0}, hit, nullptr, nullptr))
                return false;
            *out = p.y - hit.position.y;
            return true;
        };
        int checked = 0;
        for (const auto& race : l->city->races) {
            game::RaceConfig config;
            config.city = l->city->info.mapName;
            config.mode = gameMode(race.mode);
            config.raceIndex = race.index;
            config.opponents = 8;
            auto s = game::session::loadRaceSetup(config, *l->city, vfs);
            if (!s)
                continue;
            enum class Kind { Player, Racer, Police };
            struct Start {
                std::string vehicle;
                Mat34 at;
                Kind kind;
            };
            std::vector<Start> starts{{"vpbug", s->playerSpawn, Kind::Player}};
            for (const auto& o : s->opponents)
                starts.push_back({o.vehicle, o.spawn, Kind::Racer});
            for (const auto& p : s->police)
                starts.push_back({p.vehicle, p.spawn, Kind::Police});
            for (const auto& st : starts) {
                float above = 0.0f;
                if (!heightAboveGround(st.at.m3, &above))
                    continue;
                auto& car = cars[st.vehicle];
                if (!car) {
                    std::string error;
                    car = game::SimVehicle::load(vfs, st.vehicle, &error);
                    ASSERT_TRUE(car) << error;
                }
                car->addTo(*l->world);
                car->setResetPos(st.at);
                car->reset();
                const std::string where = std::format("{} race {} {}", name, race.index, st.vehicle);
                if (st.kind != Kind::Police) {
                    const Vec3 from = st.kind == Kind::Player ? car->sim().body.ics.matrix.m3
                                                              : car->sim().modelMatrix().m3;
                    phys::RayHit hit;
                    const bool hits = l->world->wheelProbe(from + Vec3{0, 2, 0}, from - Vec3{0, 10, 0}, hit,
                                                           nullptr, nullptr);
                    EXPECT_EQ(car->settleOnGround(*l->world, from), hits) << where;
                    if (hits) {
                        const float y = hit.position.y + 0.9f;
                        EXPECT_EQ(car->sim().body.ics.matrix.m3.y, car->sim().centerOfGravity.y + y) << where;
                    }
                }
                for (int i = 0; i < 90; ++i) {
                    car->sim().setInputs(0.0f, 1.0f, 0.0f, 0.0f);
                    l->world->advanceFixed(1.0f / 30.0f);
                }
                const Mat34 m = car->sim().modelMatrix();
                float settled = 0.0f;
                const bool onGround = heightAboveGround(m.m3 + Vec3{0, 1, 0}, &settled);
                if (verbose)
                    std::printf("%s %s %d %s: start %.3f above the road, after 3 s origin %.3f, up.y %.3f\n",
                                name, city::raceModeName(race.mode), race.index, st.vehicle.c_str(), above,
                                settled - 1.0f, m.m1.y);
                EXPECT_TRUE(onGround) << where;
                EXPECT_GT(m.m1.y, 0.9f) << where;
                EXPECT_LT(std::abs(settled - 1.0f), 0.5f) << where;
                car->removeFrom(*l->world);
                ++checked;
            }
        }
        EXPECT_GT(checked, 50) << name;
    }
}
