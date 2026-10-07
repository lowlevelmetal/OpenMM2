#include "TestData.h"
#include "ai/World.h"
#include "city/CityData.h"
#include "game/CityCollision.h"
#include "game/PlayerVehicle.h"
#include "game/TrafficBodies.h"

#include <gtest/gtest.h>

using namespace mm2;

// A player car pushed into the back of an ambient car turns it into a rigid
// body that the impact pushes forward.
TEST(TrafficBodies, ContactActivatesAndPushesTrafficCar) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto city = city::loadCity(vfs, "london");
    ASSERT_TRUE(city);
    auto collision = game::buildCityCollision(*city, vfs);
    phys::World world(std::move(collision.materials));
    world.setStatic(std::move(collision.soup));

    ai::Settings settings;
    settings.trafficDensity = 1.0f;
    settings.pedestrianDensity = 0.0f;
    auto ai = ai::World::create(*city, vfs, settings);
    ASSERT_TRUE(ai);
    const Vec3 centre{430, 0, -150};
    for (int i = 0; i < 60; ++i)
        ai->update(1.0f / 30.0f, centre, {});
    ASSERT_FALSE(ai->cars().empty());

    // The first car on the surface (not in a tunnel).
    const ai::AmbientCar* target = nullptr;
    for (const auto& c : ai->cars())
        if (std::abs(c.transform.m3.y) < 1.0f) {
            target = &c;
            break;
        }
    ASSERT_TRUE(target);
    const int id = target->id;
    const Mat34 carFrame = target->transform;
    const Vec3 forward = -carFrame.m2;
    const float length = target->data ? target->data->size.z : 4.5f;

    std::string error;
    auto player = game::SimVehicle::load(vfs, "vpbug", &error);
    ASSERT_TRUE(player) << error;
    player->addTo(world);
    Mat34 start = carFrame;
    start.m3 = carFrame.m3 - forward * (length * 0.5f + 1.9f); // bumpers just touching
    player->reset(start);
    player->sim().body.ics.applyImpulseNow(forward * (player->sim().body.ics.mass * 8.0f), player->sim().body.ics.matrix.m3);

    game::TrafficBodies traffic(*ai, world);
    phys::Body* vehicles[] = {&player->sim().body};
    bool activated = false;
    for (int i = 0; i < 30 && !activated; ++i) {
        traffic.beforeStep(vehicles);
        world.step(1.0f / 120.0f);
        traffic.afterStep(player->sim().modelMatrix().m3);
        activated = traffic.transformOf(id) != nullptr;
    }
    ASSERT_TRUE(activated) << "the touched traffic car did not become physical";
    for (int i = 0; i < 60; ++i) {
        world.step(1.0f / 120.0f);
        traffic.afterStep(player->sim().modelMatrix().m3);
    }
    const Mat34* moved = traffic.transformOf(id);
    ASSERT_TRUE(moved);
    const float pushed = (moved->m3 - carFrame.m3).dot(forward);
    EXPECT_GT(pushed, 0.2f) << "the hit car should move forward";
    EXPECT_LT(std::abs(moved->m3.y - carFrame.m3.y), 1.0f) << "the hit car should stay on the road";

    // Once it has come to rest (15 still physics steps) the AI takes it back:
    // standing upright on the road it drives back onto its lane
    // (aiVehicleActive::Detach -> aiGoalRegainRail -> aiGoalRandomDrive).
    const Vec3 away = player->sim().modelMatrix().m3; // stays in the same room
    bool handedBack = false, backOnRail = false;
    for (int i = 0; i < 120 * 20 && !backOnRail; ++i) {
        world.step(1.0f / 120.0f);
        traffic.afterStep(away);
        if (i % 4 == 0)
            ai->update(1.0f / 30.0f, away, {});
        handedBack = handedBack || traffic.transformOf(id) == nullptr;
        for (const auto& c : ai->cars())
            if (c.id == id && handedBack && c.goal == ai::AmbientGoal::RandomDrive)
                backOnRail = true;
    }
    EXPECT_TRUE(handedBack) << "the car should come to rest";
    EXPECT_TRUE(backOnRail) << "the car should regain its lane";
}
