// A shared-traffic client's received cars as TrafficBodies' traffic
// (game/net/NetTrafficCars): the cars its own car knocks loose ahead of the
// host, and how they go back to the host's messages.
#include "game/net/NetTrafficCars.h"

#include <gtest/gtest.h>

using namespace mm2;
using game::NetTrafficCars;

namespace {

NetTrafficCars::Received car(int id, int spawns, std::uint32_t stateTime, bool onRail = true) {
    NetTrafficCars::Received r;
    r.car.id = id;
    r.car.spawns = spawns;
    r.car.transform = Mat34::translation({static_cast<float>(id), 0, 0});
    r.car.speed = 10.0f;
    r.car.goal = onRail ? ai::AmbientGoal::RandomDrive : ai::AmbientGoal::Collision;
    r.car.physical = !onRail;
    r.stateTime = stateTime;
    return r;
}

const ai::AmbientCar* listed(const NetTrafficCars& cars, int id) {
    for (const auto& c : cars.cars())
        if (c.id == id)
            return &c;
    return nullptr;
}

} // namespace

TEST(NetTrafficCars, OnlyTheLocalCarKnocksACarLoose) {
    NetTrafficCars cars;
    EXPECT_TRUE(cars.attachable(1, true));
    EXPECT_FALSE(cars.attachable(1, false)); // the police, the other players: the host's
}

TEST(NetTrafficCars, TheCarsOffTheirRailsAreLeftToTheHostsMessages) {
    NetTrafficCars cars;
    const std::vector<NetTrafficCars::Received> received{car(1, 0, 1000), car(2, 0, 1000, false)};
    cars.update(received, 1100.0, 200.0);
    ASSERT_EQ(cars.cars().size(), 1u);
    EXPECT_EQ(cars.cars()[0].id, 1);
}

TEST(NetTrafficCars, AKnockIsConfirmedWhenTheHostKnocksTheCarToo) {
    NetTrafficCars cars;
    cars.update(std::vector{car(1, 3, 1000)}, 1100.0, 200.0);
    cars.impact(1); // the local car hit it in the step
    EXPECT_TRUE(cars.knocked(1));
    EXPECT_EQ(cars.takeKnocks(), std::vector<int>{1});
    Mat34 moved = Mat34::translation({2, 0, 1});
    cars.setPhysicalTransform(1, moved);
    // Still on its rail in the host's older messages: the local body leads.
    cars.update(std::vector{car(1, 3, 1150)}, 1200.0, 200.0);
    const ai::AmbientCar* c = listed(cars, 1);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->physical);
    EXPECT_EQ(c->transform.m3, moved.m3);
    EXPECT_TRUE(cars.takeHandovers().empty());
    // The host knocked it too; while the local body moves it still leads
    // (it runs the host's physics from the same hit).
    cars.update(std::vector{car(1, 3, 1250, false)}, 1350.0, 200.0);
    EXPECT_TRUE(cars.knocked(1));
    EXPECT_EQ(cars.stats().confirmed, 1u);
    EXPECT_TRUE(cars.takeHandovers().empty());
    // At rest: the host's messages lead from now.
    const Mat34 rest = Mat34::translation({3, 0, 2});
    cars.detach(1, rest, true);
    cars.update(std::vector{car(1, 3, 1400, false)}, 1500.0, 200.0);
    EXPECT_FALSE(cars.knocked(1));
    EXPECT_EQ(listed(cars, 1), nullptr); // off its rail: a moving instance
    const auto h = cars.takeHandovers();
    ASSERT_EQ(h.size(), 1u);
    EXPECT_TRUE(h[0].confirmed);
    EXPECT_EQ(h[0].pose.m3, rest.m3);
    EXPECT_EQ(cars.stats().confirmed, 1u);
}

// A local body far from where the host has the knocked car goes back to the
// host's messages at once.
TEST(NetTrafficCars, AKnockFarFromTheHostsGoesBackAtOnce) {
    NetTrafficCars cars;
    cars.update(std::vector{car(1, 3, 1000)}, 1100.0, 200.0);
    cars.impact(1);
    cars.setPhysicalTransform(1, Mat34::translation({20, 0, 0}));
    cars.update(std::vector{car(1, 3, 1150, false)}, 1250.0, 200.0);
    EXPECT_FALSE(cars.knocked(1));
    ASSERT_EQ(cars.takeHandovers().size(), 1u);
}

TEST(NetTrafficCars, AKnockTheHostNeverMakesIsWithdrawn) {
    NetTrafficCars cars;
    cars.update(std::vector{car(1, 3, 1000)}, 1100.0, 200.0);
    cars.impact(1);
    cars.update(std::vector{car(1, 3, 1290)}, 1400.0, 200.0);
    EXPECT_TRUE(cars.knocked(1)); // 190 ms after the hit: maybe not there yet
    cars.update(std::vector{car(1, 3, 1310)}, 1420.0, 200.0);
    EXPECT_FALSE(cars.knocked(1)); // on its rail 210 ms after it on the host
    const ai::AmbientCar* c = listed(cars, 1);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->physical);
    const auto h = cars.takeHandovers();
    ASSERT_EQ(h.size(), 1u);
    EXPECT_FALSE(h[0].confirmed);
    EXPECT_EQ(cars.stats().withdrawn, 1u);
}

TEST(NetTrafficCars, ARecycledOrVanishedCarIsForgotten) {
    NetTrafficCars cars;
    cars.update(std::vector{car(1, 3, 1000), car(2, 0, 1000)}, 1100.0, 200.0);
    cars.impact(1);
    cars.impact(2);
    cars.update(std::vector{car(1, 4, 1050)}, 1150.0, 200.0); // a new car in slot 1, car 2 gone
    EXPECT_FALSE(cars.knocked(1));
    EXPECT_FALSE(cars.knocked(2));
    const ai::AmbientCar* c = listed(cars, 1);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->physical);
    EXPECT_TRUE(cars.takeHandovers().empty());
    // A car that is not listed cannot be knocked.
    cars.impact(7);
    EXPECT_FALSE(cars.knocked(7));
}
