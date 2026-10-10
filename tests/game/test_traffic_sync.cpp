// The shared ambient traffic of multiplayer cruise (game/net/TrafficSync):
// which cars the host sends each client, and how the client follows them
// through spawns, despawns, loss and reordering.
#include "game/net/NetGame.h"
#include "game/net/TrafficSync.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace mm2;
using game::SharedCar;
using game::TrafficClient;
using game::TrafficHost;

namespace {

SharedCar railCar(int id, Vec3 pos, float speed = 10.0f, int model = 1) {
    SharedCar c;
    c.id = id;
    c.model = model;
    c.paint = 2;
    c.transform = Mat34::rotationY(0.0f);
    c.transform.m3 = pos;
    c.speed = speed;
    c.velocity = -c.transform.m2 * speed; // along -Z
    return c;
}

SharedCar cop(int id, Vec3 pos, std::uint8_t target) {
    SharedCar c = railCar(id, pos, 30.0f, 3);
    c.kind = net::AmbientKind::Police;
    c.target = target;
    c.flags = net::kAmbientSiren | net::kAmbientPursuit;
    c.angularVelocity = {0, 0.5f, 0};
    return c;
}

// Host -> wire -> client, as the race does.
net::AmbientStateMsg wire(const net::AmbientStateMsg& m) {
    net::AmbientStateMsg out;
    EXPECT_TRUE(net::decodeMessage(net::encodeMessage(m), out));
    return out;
}

net::AmbientStateMsg message(std::uint32_t time, std::initializer_list<SharedCar> cars,
                             std::uint32_t steps = 0) {
    TrafficHost host;
    std::vector<SharedCar> list(cars);
    return wire(host.build({1, {0, 0, 0}}, list, time, steps, 0x1234));
}

const TrafficClient::Car* find(const TrafficClient& c, int id) {
    for (const auto& car : c.cars())
        if (car.id == id)
            return &car;
    return nullptr;
}

} // namespace

TEST(TrafficCatalog, IndicesAndChecksum) {
    game::TrafficCatalog a, b;
    for (auto* m : {"vacompact_l", "VAPICKUP_S", "vpcop"})
        a.add(m);
    a.add("vacompact_l"); // duplicate
    for (auto* m : {"vacompact_l", "vapickup_s", "vpcop"})
        b.add(m);
    EXPECT_EQ(a.size(), 3u);
    EXPECT_EQ(a.find("vapickup_s"), 1);
    EXPECT_EQ(a.find("nope"), -1);
    ASSERT_NE(a.name(2), nullptr);
    EXPECT_EQ(*a.name(2), "vpcop");
    EXPECT_EQ(a.name(3), nullptr);
    EXPECT_EQ(a.name(-1), nullptr);
    EXPECT_EQ(a.checksum(), b.checksum());
    game::TrafficCatalog c;
    for (auto* m : {"vapickup_s", "vacompact_l", "vpcop"})
        c.add(m);
    EXPECT_NE(a.checksum(), c.checksum()); // the order matters
}

TEST(TrafficHost, SendsTheCarsNearEachClientWithHysteresis) {
    TrafficHost host;
    std::vector<SharedCar> cars = {railCar(0, {10, 0, 0}),  railCar(1, {150, 0, 0}),
                                   railCar(2, {215, 0, 0}), railCar(3, {400, 0, 0}),
                                   railCar(4, {-50, 0, 5}), railCar(600, {1, 0, 1}),
                                   railCar(5, {1, 0, 2}, 1.0f, 99)};
    auto ids = [](const net::AmbientStateMsg& m) {
        std::vector<int> v;
        for (const auto& e : m.entities)
            v.push_back(e.id);
        return v;
    };
    const game::TrafficViewer viewer{1, {0, 0, 0}};
    auto m = host.build(viewer, cars, 100, 7, 0xAAAA);
    EXPECT_EQ(m.time, 100u);
    EXPECT_EQ(m.lightSteps, 7u);
    EXPECT_EQ(m.catalog, 0xAAAA);
    // Nearest first; beyond 200 m out; ids and models out of range skipped.
    EXPECT_EQ(ids(m), (std::vector<int>{0, 4, 1}));
    // Car 1 drifts to 220 m: still sent (it is in the set until 230 m).
    cars[1].transform.m3.x = 220.0f;
    // Car 2 comes to 205 m: not in the set, so not yet.
    cars[2].transform.m3.x = 205.0f;
    m = host.build(viewer, cars, 150, 8, 0xAAAA);
    EXPECT_EQ(ids(m), (std::vector<int>{0, 4, 1}));
    cars[1].transform.m3.x = 240.0f;
    cars[2].transform.m3.x = 190.0f;
    m = host.build(viewer, cars, 200, 9, 0xAAAA);
    EXPECT_EQ(ids(m), (std::vector<int>{0, 4, 2}));
    // Every client has its own set.
    m = host.build({2, {400, 0, 0}}, cars, 200, 9, 0xAAAA);
    EXPECT_EQ(ids(m), (std::vector<int>{3, 1})); // car 2 is 210 m from it
}

TEST(TrafficHost, ChasingPoliceComeFirstAndTheBudgetHolds) {
    TrafficHost host;
    std::vector<SharedCar> cars;
    for (int i = 0; i < 200; ++i)
        cars.push_back(railCar(i, {static_cast<float>(i % 20) * 3.0f, 0, static_cast<float>(i / 20) * 3.0f}));
    cars.push_back(cop(300, {450, 0, 0}, 1));  // far, chasing this client
    cars.push_back(cop(301, {450, 0, 10}, 2)); // far, chasing another one
    const auto m = host.build({1, {0, 0, 0}}, cars, 1, 0, 0);
    ASSERT_FALSE(m.entities.empty());
    EXPECT_EQ(m.entities[0].id, 300);
    EXPECT_TRUE(
        std::none_of(m.entities.begin(), m.entities.end(), [](const auto& e) { return e.id == 301; }));
    EXPECT_LE(net::encodeMessage(m).size(), host.options().maxBytes);
    EXPECT_GT(m.entities.size(), 50u);
}

// Protocol 12: at traffic density 1 (100-130 cars within 200 m in San
// Francisco's downtown) every car within the interest radius fits once the
// client knows them (the far ones carry their state in every other or fourth
// message), turning and changing speed, the police chasing the client among
// them; the message is at most two of ENet's fragments.
TEST(TrafficHost, EveryCarWithin200MetresFitsAtDensityOne) {
    TrafficHost host;
    std::vector<SharedCar> cars;
    for (int i = 0; i < 150; ++i) {
        const float angle = static_cast<float>(i) * 2.4f;
        const float r = 10.0f + static_cast<float>(i) * 1.25f; // out to 196 m
        SharedCar c = railCar(i, {r * std::cos(angle), 0, r * std::sin(angle)});
        c.motion.accel = 1.5f;
        c.motion.curvature = 0.05f;
        cars.push_back(c);
    }
    cars.push_back(cop(300, {40, 0, 0}, 1));
    cars.push_back(cop(301, {-60, 0, 5}, 1));
    net::AmbientStateMsg m;
    for (std::uint32_t t = 0; t < 8; ++t)
        m = host.build({1, {0, 0, 0}}, cars, t * 50, 0, 0);
    EXPECT_EQ(m.entities.size(), cars.size());
    const std::size_t bytes = net::encodeMessage(m).size();
    EXPECT_LE(bytes, host.options().maxBytes);
    EXPECT_LE(host.options().maxBytes, 2u * 1392u - 100u); // two fragments, their headers included
    EXPECT_LE(m.entities.size(), net::kMaxAmbientPerMessage);
}

TEST(TrafficClient, FollowsSpawnUpdateAndDespawn) {
    TrafficClient client(8, 0x1234);
    client.receive(message(1000, {railCar(5, {0, 0, 0})}, 30));
    client.update(950.0); // before the car's first state: hidden
    EXPECT_EQ(client.cars().size(), 0u);
    client.receive(message(1050, {railCar(5, {0, 0, -0.5f}), railCar(6, {20, 0, 0})}, 31));
    client.update(1025.0);
    const auto* car = find(client, 5);
    ASSERT_NE(car, nullptr);
    EXPECT_NEAR(car->transform.m3.z, -0.25f, 0.05f);
    EXPECT_FALSE(car->extrapolated);
    EXPECT_TRUE(car->fresh);
    EXPECT_NEAR(car->speed, 10.0f, 0.1f);
    EXPECT_EQ(find(client, 6), nullptr); // not until 1050
    client.update(1050.0);
    ASSERT_NE(find(client, 6), nullptr);
    EXPECT_FALSE(find(client, 5)->fresh);
    // Car 5 left: gone once the render time reaches that message.
    client.receive(message(1100, {railCar(6, {20, 0, -0.5f})}, 32));
    client.update(1075.0);
    EXPECT_NE(find(client, 5), nullptr);
    client.update(1100.0);
    EXPECT_EQ(find(client, 5), nullptr);
    EXPECT_NE(find(client, 6), nullptr);
    EXPECT_EQ(client.known(), 1u);
    // The light steps run on from the newest message in 1/30 s steps.
    EXPECT_EQ(client.lightSteps(1100.0), 32u);
    EXPECT_EQ(client.lightSteps(1200.0), 35u);
}

TEST(TrafficClient, LossIsBridgedAndReorderingNeverResurrects) {
    TrafficClient client(8, 0x1234);
    client.receive(message(0, {railCar(1, {0, 0, 0}), railCar(2, {5, 0, 0})}));
    // 50 and 100 lost: car 1 extrapolates along its velocity.
    client.update(120.0);
    const auto* c1 = find(client, 1);
    ASSERT_NE(c1, nullptr);
    EXPECT_TRUE(c1->extrapolated);
    EXPECT_NEAR(c1->transform.m3.z, -1.2f, 0.05f);
    // Then 150 arrives, without car 2; later the lost 100 shows up late.
    client.receive(message(150, {railCar(1, {0, 0, -1.5f})}));
    client.receive(
        message(100, {railCar(1, {0, 0, -1.0f}), railCar(2, {5, 0, -1.0f}), railCar(3, {9, 0, 0})}));
    EXPECT_EQ(client.stats().outdated, 1u);
    // Its state of car 1 is used ...
    client.update(125.0);
    EXPECT_NEAR(find(client, 1)->transform.m3.z, -1.25f, 0.05f);
    EXPECT_FALSE(find(client, 1)->extrapolated);
    EXPECT_NE(find(client, 2), nullptr); // (still there until 150)
    EXPECT_EQ(find(client, 3), nullptr); // but it adds no car the newer message lacks
    client.update(150.0);
    EXPECT_EQ(find(client, 2), nullptr); // and does not bring one back
    // Nothing for a long while: the car is dropped.
    client.update(150.0 + client.options().staleMs + 1.0);
    EXPECT_EQ(client.cars().size(), 0u);
    EXPECT_EQ(client.known(), 0u);
}

TEST(TrafficClient, ARecycledSlotIsANewCar) {
    TrafficClient client(8, 0x1234);
    SharedCar a = railCar(4, {0, 0, 0});
    client.receive(message(0, {a}));
    a.transform.m3.z = -0.5f;
    client.receive(message(50, {a}));
    client.update(50.0);
    // The pool slot is reused far away: same id, next generation.
    SharedCar b = railCar(4, {150, 0, 100}, 0.0f, 2);
    b.generation = 1;
    client.receive(message(100, {b}));
    client.update(75.0); // between the two: never blended across the city
    EXPECT_EQ(find(client, 4), nullptr);
    client.update(100.0);
    const auto* car = find(client, 4);
    ASSERT_NE(car, nullptr);
    EXPECT_NEAR(car->transform.m3.x, 150.0f, 0.05f);
    EXPECT_EQ(car->model, 2);
    EXPECT_TRUE(car->fresh);
    // Moved further than its speed explains without a new generation (a
    // police car back at its post): shown there at once.
    SharedCar c = b;
    c.transform.m3 = {-150, 0, 0};
    client.receive(message(150, {c}));
    client.update(125.0);
    EXPECT_EQ(find(client, 4), nullptr);
    client.update(150.0);
    EXPECT_NEAR(find(client, 4)->transform.m3.x, -150.0f, 0.05f);
    EXPECT_EQ(client.stats().teleports, 1u);
}

TEST(TrafficClient, BadEntriesAreRefused) {
    TrafficClient client(3, 0x1234);
    net::AmbientStateMsg m;
    m.time = 10;
    m.catalog = 0x9999; // another catalog
    net::AmbientEntity good;
    good.id = 1;
    good.model = 2;
    good.position = {1, 2, 3};
    net::AmbientEntity badModel = good;
    badModel.id = 2;
    badModel.model = 3; // beyond the client's catalog
    net::AmbientEntity duplicate = good;
    duplicate.position = {100, 0, 0};
    net::AmbientEntity nan = good;
    nan.id = 3;
    nan.position.x = std::numeric_limits<float>::quiet_NaN();
    net::AmbientEntity badId = good;
    badId.id = 900;
    m.entities = {good, badModel, duplicate, nan, badId};
    client.receive(m);
    client.update(10.0);
    EXPECT_TRUE(client.catalogMismatch());
    ASSERT_EQ(client.cars().size(), 1u);
    EXPECT_NEAR(client.cars()[0].transform.m3.x, 1.0f, 1e-4f);
    EXPECT_EQ(client.stats().refused, 4u);
}

TEST(TrafficClient, PoliceKeepTheirChaseAndTheHornIsAnEdge) {
    TrafficClient client(8, 0x1234);
    SharedCar p = cop(300, {0, 0, 0}, 3);
    p.rpm = 4000.0f;
    p.gear = 2;
    SharedCar t = railCar(9, {5, 0, 0});
    t.flags = net::kAmbientHorn;
    client.receive(message(0, {p, t}));
    p.transform.m3.z = -1.5f;
    t.flags = 0;
    client.receive(message(50, {p, t}));
    client.update(10.0);
    const auto* c = find(client, 300);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->kind, net::AmbientKind::Police);
    EXPECT_EQ(c->target, 3);
    EXPECT_NEAR(c->rpm, 4000.0f, 40.0f);
    EXPECT_EQ(c->gear, 2);
    EXPECT_TRUE(c->flags & net::kAmbientSiren);
    EXPECT_TRUE(find(client, 9)->hornStarted);
    client.update(15.0);
    EXPECT_FALSE(find(client, 9)->hornStarted); // once
}

TEST(TrafficSettings, TheLobbyOptionCrossesTheWire) {
    game::RaceConfig on;
    on.mode = game::GameMode::Cruise;
    on.trafficDensity = 0.75f;
    on.copDensity = 0.25f;
    EXPECT_TRUE(on.netTraffic); // on by default
    const auto s = game::toSessionSettings(on, "Cruise", 8);
    EXPECT_TRUE(s.sharedTraffic);
    const auto back = game::fromSessionSettings(s);
    EXPECT_TRUE(back.netTraffic);
    EXPECT_NEAR(back.trafficDensity, 0.75f, 0.01f);
    EXPECT_NEAR(back.copDensity, 0.25f, 0.01f);
    game::RaceConfig off = on;
    off.netTraffic = false;
    EXPECT_FALSE(game::fromSessionSettings(game::toSessionSettings(off, "Cruise", 8)).netTraffic);
}

// A hostile host: random messages (decoded from noise, as the session would
// hand them over) never put a car out of range, and the client keeps no more
// cars than the ids allow.
TEST(TrafficClient, NoiseNeverYieldsABadCar) {
    TrafficClient client(10, 0x1234);
    std::uint32_t seed = 99;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    // A valid message with bits flipped (the length kept, so most decode).
    std::vector<SharedCar> cars;
    for (int i = 0; i < 20; ++i)
        cars.push_back(i % 5 == 0 ? cop(300 + i, {static_cast<float>(i), 0, 0}, 1)
                                  : railCar(i, {static_cast<float>(i) * 3.0f, 0, 5}));
    TrafficHost host;
    const auto valid = net::encodeMessage(host.build({1, {0, 0, 0}}, cars, 0, 0, 0x1234));
    std::size_t decodedCount = 0;
    for (int round = 0; round < 4000; ++round) {
        std::vector<std::byte> junk = valid;
        for (int k = 0, n = 1 + static_cast<int>(next() % 12); k < n; ++k)
            junk[1 + next() % (junk.size() - 1)] ^= static_cast<std::byte>(1u << (next() % 8));
        net::AmbientStateMsg m;
        if (!net::decodeMessage(junk, m))
            continue;
        ++decodedCount;
        m.time = static_cast<std::uint32_t>(round * 50); // a believable clock
        client.receive(m);
        client.update(static_cast<double>(round * 50) - 100.0);
        for (const auto& c : client.cars()) {
            ASSERT_LT(c.id, static_cast<int>(net::kMaxAmbientIds));
            ASSERT_LT(c.model, 10);
            ASSERT_LE(c.paint, static_cast<int>(net::kMaxAmbientPaint));
            ASSERT_TRUE(std::isfinite(c.transform.m3.x) && std::isfinite(c.transform.m3.y) &&
                        std::isfinite(c.transform.m3.z) && std::isfinite(c.speed));
            ASSERT_TRUE(c.target == net::kAmbientNoTarget || c.target < net::kMaxPlayers);
        }
        ASSERT_LE(client.known(), static_cast<std::size_t>(net::kMaxAmbientIds));
    }
    EXPECT_GT(decodedCount, 0u);
}

TEST(TrafficHost, FarCarsComeWithTheirStateEveryOtherMessage) {
    TrafficHost host;
    std::vector<SharedCar> cars = {railCar(1, {10, 0, 0}), railCar(10, {150, 0, 0}), railCar(11, {0, 0, 150}),
                                   cop(300, {0, 0, 160}, 2)};
    auto states = [&](const net::AmbientStateMsg& m) {
        std::vector<std::pair<int, bool>> v;
        for (const auto& e : m.entities)
            v.emplace_back(e.id, e.hasState);
        std::ranges::sort(v);
        return v;
    };
    const game::TrafficViewer viewer{1, {0, 0, 0}};
    // New to the client: everything with its state.
    auto m = wire(host.build(viewer, cars, 0, 0, 0));
    EXPECT_EQ(states(m), (std::vector<std::pair<int, bool>>{{1, true}, {10, true}, {11, true}, {300, true}}));
    // Then the far rail cars alternate; the near one and the police always.
    m = wire(host.build(viewer, cars, 50, 0, 0));
    EXPECT_EQ(states(m), (std::vector<std::pair<int, bool>>{{1, true}, {10, false}, {11, true}, {300, true}}));
    m = wire(host.build(viewer, cars, 100, 0, 0));
    EXPECT_EQ(states(m), (std::vector<std::pair<int, bool>>{{1, true}, {10, true}, {11, false}, {300, true}}));
    // A far car knocked off its rail: every message.
    cars[1].flags |= net::kAmbientOffRail;
    m = wire(host.build(viewer, cars, 150, 0, 0));
    EXPECT_EQ(states(m), (std::vector<std::pair<int, bool>>{{1, true}, {10, true}, {11, true}, {300, true}}));
}

// When the cars near a client do not all fit, the ones it has stay ahead of
// new ones a little nearer, so the car at the edge of what fits does not
// come and go; beyond 180 m a known car gets its state every fourth message.
TEST(TrafficHost, TheCarsThatFitStayAndTheFarthestComeEveryFourthMessage) {
    game::TrafficHost::Options options;
    options.maxBytes = 70; // the header and three cars with their state
    TrafficHost host(options);
    std::vector<SharedCar> cars = {railCar(1, {10, 0, 0}), railCar(2, {20, 0, 0}), railCar(3, {60, 0, 0}),
                                   railCar(4, {75, 0, 0})};
    auto ids = [](const net::AmbientStateMsg& m) {
        std::vector<int> v;
        for (const auto& e : m.entities)
            v.push_back(e.id);
        return v;
    };
    const game::TrafficViewer viewer{1, {0, 0, 0}};
    EXPECT_EQ(ids(host.build(viewer, cars, 0, 0, 0)), (std::vector<int>{1, 2, 3}));
    // Car 4 comes 10 m nearer than car 3: car 3 stays.
    cars[3].transform.m3.x = 50.0f;
    EXPECT_EQ(ids(host.build(viewer, cars, 50, 0, 0)), (std::vector<int>{1, 2, 3}));
    // 30 m nearer: car 4 takes its place.
    cars[3].transform.m3.x = 30.0f;
    EXPECT_EQ(ids(host.build(viewer, cars, 100, 0, 0)), (std::vector<int>{1, 2, 4}));

    TrafficHost far;
    const std::vector<SharedCar> distant = {railCar(10, {190, 0, 0})};
    int states = 0;
    for (std::uint32_t i = 0; i < 9; ++i)
        states += far.build(viewer, distant, i * 50, 0, 0).entities.at(0).hasState ? 1 : 0;
    EXPECT_EQ(states, 3); // new, then every fourth of the next eight
}

TEST(TrafficClient, ACarWithoutItsStateIsStillThere) {
    TrafficClient client(8, 0x1234);
    client.receive(message(0, {railCar(10, {150, 0, 0}), railCar(11, {0, 0, 150})}));
    auto held = [](std::uint32_t time, std::initializer_list<std::pair<int, int>> ids) {
        net::AmbientStateMsg m;
        m.time = time;
        m.catalog = 0x1234;
        for (const auto& [id, gen] : ids) {
            net::AmbientEntity e;
            e.id = static_cast<std::uint16_t>(id);
            e.generation = static_cast<std::uint8_t>(gen);
            e.hasState = false;
            m.entities.push_back(e);
        }
        return wire(m);
    };
    // 10 and 11 only said to be there (and 12, unknown, waits for its state).
    client.receive(held(50, {{10, 0}, {11, 0}, {12, 0}}));
    client.update(50.0);
    ASSERT_NE(find(client, 10), nullptr);
    ASSERT_NE(find(client, 11), nullptr);
    EXPECT_EQ(find(client, 12), nullptr);
    EXPECT_TRUE(find(client, 10)->extrapolated);
    EXPECT_NEAR(find(client, 10)->transform.m3.z, -0.5f, 0.05f); // on along its velocity
    // 11's slot now holds another car (another generation): the old one has gone.
    client.receive(held(100, {{10, 0}, {11, 1}}));
    client.update(100.0);
    EXPECT_NE(find(client, 10), nullptr);
    EXPECT_EQ(find(client, 11), nullptr);
}
