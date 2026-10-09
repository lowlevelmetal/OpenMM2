// The host's props in a network race (game/net/PropSync, net/PropState.h):
// a host and a client machine in one process, each with its own world and
// props, the messages through an in-process link with latency and loss.

#include "TestData.h"

#include "ai/World.h"
#include "city/CityData.h"
#include "game/CityLevel.h"
#include "game/RaceConfig.h"
#include "game/VehicleRenderer.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/net/NetGame.h"
#include "game/net/NetProps.h"
#include "game/net/PropSync.h"
#include "game/world/Gizmos.h"
#include "net/PropState.h"
#include "phys/Bound.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <span>

using namespace mm2;
using namespace mm2::game;
using bangers::BangerSet;
using bangers::PlacedProp;

namespace {

constexpr float kDt = 1.0f / 60.0f;
constexpr double kStepMs = 1000.0 / 60.0;

// A temporary game folder with a few banger files (as test_bangers.cpp).
struct TempBangers {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("openmm2_propsync_") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    vfs::Vfs vfs;
    TempBangers() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "tune" / "banger");
        write("light", 80.0f, 100.0f * 100.0f, 0); // breaks above 100 N s
        write("heavy", 50000.0f, 1e15f, 0);
        write("split", 40.0f, 100.0f * 100.0f, 2);
        write("split_break01", 20.0f, 1e6f, 0, {0.5f, 1.0f, 0.5f}, {0, 0.5f, 0});
        write("split_break02", 20.0f, 1e6f, 0, {0.5f, 1.0f, 0.5f}, {0, 1.5f, 0});
        write("car_break0", 15.0f, 1e6f, 0, {0.6f, 0.3f, 0.6f}, {0, 0.15f, 0});
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~TempBangers() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const char* name, float mass, float limit2, int parts, Vec3 size = {0.5f, 2.0f, 0.5f},
               Vec3 cg = {0, 1, 0}) {
        std::ofstream f(dir / "tune" / "banger" / (std::string(name) + ".dgbangerdata"));
        f << "type: a\ndgBangerData {\n  Size " << size.x << " " << size.y << " " << size.z;
        f << "\n  CG " << cg.x << " " << cg.y << " " << cg.z << "\n  Mass " << mass;
        f << "\n  Elasticity 0.5\n  Friction 0.9";
        f << "\n  ImpulseLimit2 " << limit2 << "\n  NumParts " << parts;
        f << "\n  BirthRule {\n    InitialBlast 0\n  }\n  TexNumber 0\n  CollisionPrim 1\n}\n";
    }
};

// One room with a floor whose objects are the set's props.
class PropLevel final : public phys::Level {
public:
    explicit PropLevel(const InstanceSource& source) : m_source(source) {}
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override {
        out.clear();
        const Vec3 floor[4] = {{-500, 0, -500}, {-500, 0, 500}, {500, 0, 500}, {500, 0, -500}};
        out.addPolygon(floor, 4, {0, 1, 0}, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        m_source.instancesIn(room, out);
    }

private:
    const InstanceSource& m_source;
};

// A box "car" 1.8 x 1.2 x 4 m of 1000 kg moving level (no gravity).
struct Car {
    std::unique_ptr<phys::BoundBox> bound = std::make_unique<phys::BoundBox>(Vec3{1.8f, 1.2f, 4.0f});
    phys::Body body;
    Car(const Vec3& pos, const Vec3& vel) {
        bound->makeOwnMaterial();
        body.ics.setMass(1.8f, 1.2f, 4.0f, 1000.0f);
        body.collisionBound = bound.get();
        body.place(Mat34::translation(pos));
        body.ics.gravity = {0, 0, 0};
        body.ics.linearVelocity = vel;
        body.ics.linearMomentum = vel * 1000.0f;
    }
};

// One machine: its world and its props.
struct Machine {
    phys::World world;
    BangerSet set;
    PropLevel level{set};
    explicit Machine(const bangers::BangerDataLibrary& lib) : set(lib) {
        world.setLevel(&level);
        set.setWorld(&world);
        set.recordKnocks(true);
    }
    ~Machine() { set.setWorld(nullptr); }
    void place(const std::vector<PlacedProp>& props) { set.add(props); }
    void step() {
        world.step(kDt);
        set.update(kDt);
    }
};

std::vector<PlacedProp> layout() {
    auto at = [](const char* model, const Vec3& p) {
        return PlacedProp{model, Mat34::translation(p), 1, PlacedProp::Source::Instance, true};
    };
    // A row of props across the cars' path at z = 0, and one far away.
    return {at("light", {0, 0, 0}), at("light", {6, 0, 0}), at("split", {12, 0, 0}), at("heavy", {18, 0, 0}),
            at("light", {200, 0, 0})};
}

// Host -> client through a link of `latencyMs` (+ up to `jitterMs`) that
// loses `loss` of the unreliable messages (the knocks are reliable).
struct Network {
    double latencyMs = 60.0, jitterMs = 0.0, loss = 0.0;
    std::mt19937 rng{7};
    struct Packet {
        double at = 0;
        bool event = false;
        std::vector<std::byte> bytes;
    };
    std::deque<Packet> inFlight;
    void send(double now, std::vector<std::byte> bytes, bool event) {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (!event && u(rng) < loss)
            return;
        inFlight.push_back({now + latencyMs + u(rng) * jitterMs, event, std::move(bytes)});
    }
    template <class F>
    void deliver(double now, F&& f) {
        std::stable_sort(inFlight.begin(), inFlight.end(),
                         [](const Packet& a, const Packet& b) { return a.at < b.at; });
        while (!inFlight.empty() && inFlight.front().at <= now) {
            f(inFlight.front());
            inFlight.pop_front();
        }
    }
};

// The host and one client of a network race on the same props.
struct Race {
    TempBangers files;
    bangers::BangerDataLibrary lib{files.vfs};
    Machine host{lib}, client{lib};
    PropHost propHost;
    std::optional<PropClient> propClient;
    Network net;
    double now = 10000.0; // session ms
    PropClient::CarPartResolver resolver;
    const phys::Instance* clientCar = nullptr;

    Race() {
        host.place(layout());
        client.place(layout());
        propClient.emplace(propCatalog(client.set));
        client.set.setReplica([this](const phys::Instance& other) { return &other == clientCar; });
    }
    // One frame on both machines, in the race screen's order.
    void frame() {
        now += kStepMs;
        // Host: the step, the knocks and the ring to the client.
        host.step();
        const auto knocks = host.set.takeKnocks();
        const auto time = static_cast<std::uint32_t>(now);
        propHost.knocked(knocks, time);
        for (auto& e : propHost.takeKnockEvents())
            net.send(now, std::move(e), true);
        if (auto msg = propHost.build(host.set, time, static_cast<std::uint64_t>(now), propCatalog(host.set)))
            net.send(now, net::encodeMessage(*msg), false);
        // Client: what arrived, the host's props placed, its step, its own
        // knocks.
        net.deliver(now, [&](const Network::Packet& p) {
            if (p.event) {
                net::PropKnocksEvent e;
                ASSERT_TRUE(net::decodePayload(p.bytes, e));
                propClient->receiveKnocks(e);
            } else {
                net::PropStateMsg m;
                ASSERT_TRUE(net::decodeMessage(p.bytes, m));
                propClient->receive(m, now);
            }
        });
        propClient->update(client.set, now, resolver);
        client.step();
        propClient->predicted(client.set.takeKnocks(), now);
    }
    void run(double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i)
            frame();
    }
};

// Where each machine shows the pieces of placed prop `prop` (by part; -1
// the whole prop), at rest or not.
std::map<int, Vec3> piecesOf(const BangerSet& set, std::size_t prop) {
    std::map<int, Vec3> out;
    for (const auto& inst : set.instances())
        if (inst.everHit && inst.source == static_cast<int>(prop) && inst.state != BangerSet::State::Gone)
            out[inst.part] = inst.drawn.value_or(inst.matrix).m3;
    return out;
}

void expectSamePieces(const Race& r, std::size_t prop, float tolerance) {
    const auto host = piecesOf(r.host.set, prop);
    const auto client = piecesOf(r.client.set, prop);
    ASSERT_FALSE(host.empty()) << "prop " << prop;
    ASSERT_EQ(host.size(), client.size()) << "prop " << prop;
    for (const auto& [part, at] : host) {
        ASSERT_TRUE(client.contains(part)) << "prop " << prop << " part " << part;
        EXPECT_LT(at.dist(client.at(part)), tolerance) << "prop " << prop << " part " << part;
    }
}

} // namespace

// Every machine places the same props in the same order: the same catalog.
TEST(PropSync, CatalogNamesThePlacement) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine a(lib), b(lib);
    a.place(layout());
    b.place(layout());
    EXPECT_EQ(propCatalog(a.set), propCatalog(b.set));
    EXPECT_EQ(placedProps(a.set), 5u);
    auto moved = layout();
    moved[2].transform.m3.x += 0.05f;
    Machine c(lib);
    c.place(moved);
    EXPECT_NE(propCatalog(a.set), propCatalog(c.set));
}

// A prop the host's car knocks over is knocked over on the client when it
// shows the host's props at that time, and rests where the host's does.
TEST(PropSync, HostKnocksReachTheClient) {
    Race r;
    Car car({0, 1, 3.0f}, {0, 0, -10});
    r.host.world.add(&car.body);
    double knockedAt = 0, shownAt = 0;
    for (int i = 0; i < 6 * 60; ++i) {
        r.frame();
        if (knockedAt == 0 && !r.host.set.standing(0))
            knockedAt = r.now;
        if (shownAt == 0 && !r.client.set.standing(0))
            shownAt = r.now;
        if (i == 60)
            r.host.world.remove(&car.body); // gone past the props
    }
    ASSERT_GT(knockedAt, 0);
    ASSERT_GT(shownAt, 0);
    // Shown a playout delay (the link's latency and a send interval) after
    // the host's knock.
    EXPECT_GE(shownAt - knockedAt, r.net.latencyMs);
    EXPECT_LE(shownAt - knockedAt, 400.0);
    EXPECT_TRUE(r.client.set.standing(1)); // the others stand on both
    EXPECT_TRUE(r.host.set.standing(1));
    expectSamePieces(r, 0, 0.01f);
    EXPECT_EQ(r.propClient->stats().knocks, 1u);
    EXPECT_EQ(r.propClient->stats().predicted, 0u);
    // The client's car can hit the piece at rest: it is an object of its world.
    std::vector<phys::Instance*> listed;
    r.client.set.instancesIn(1, listed);
    EXPECT_EQ(listed.size(), 5u); // the four props still standing and the host's piece
}

// The host's car breaks a prop into its BREAKnn pieces: the client shows each
// piece where the host has it.
TEST(PropSync, PiecesFollowTheHost) {
    Race r;
    Car car({12, 1, 3.0f}, {0, 0, -12});
    r.host.world.add(&car.body);
    r.run(1.0);
    r.host.world.remove(&car.body);
    r.run(5.0);
    ASSERT_FALSE(r.host.set.standing(2));
    EXPECT_FALSE(r.client.set.standing(2));
    expectSamePieces(r, 2, 0.01f);
    EXPECT_EQ(piecesOf(r.client.set, 2).size(), 2u);
}

// The client's own car knocks a prop: it moves at once on the client, the
// host makes the same knock with its copy of the car (here 150 ms later),
// and once both rest the client shows the host's.
TEST(PropSync, ClientKnockIsPredictedThenHandedOver) {
    Race r;
    Car mine({6, 1, 3.0f}, {0, 0, -10});
    r.clientCar = &mine.body;
    r.client.world.add(&mine.body);
    Car copy({6, 1, 4.5f}, {0, 0, -10}); // the host's view of it, behind in time
    r.host.world.add(&copy.body);
    double predictedAt = 0;
    for (int i = 0; i < 7 * 60; ++i) {
        r.frame();
        if (predictedAt == 0 && !r.client.set.standing(1))
            predictedAt = r.now;
        if (i == 60) {
            r.client.world.remove(&mine.body);
            r.host.world.remove(&copy.body);
        }
    }
    ASSERT_GT(predictedAt, 0);
    EXPECT_FALSE(r.host.set.standing(1));
    const auto& s = r.propClient->stats();
    EXPECT_EQ(s.predicted, 1u);
    EXPECT_EQ(s.confirmed, 1u);
    EXPECT_EQ(s.undone, 0u);
    EXPECT_GE(s.handovers, 1u);
    // One piece on each machine, where the host's rests.
    expectSamePieces(r, 1, 0.01f);
    // The client's own simulated piece is gone: only the host's is shown.
    for (const auto& inst : r.client.set.instances()) {
        if (inst.everHit && inst.state != BangerSet::State::Gone) {
            EXPECT_TRUE(inst.mirror);
        }
    }
}

// A knock the host never makes (its copy of the car missed the prop) is
// undone: the prop stands again on the client.
TEST(PropSync, UnconfirmedKnockIsUndone) {
    Race r;
    Car mine({0, 1, 3.0f}, {0, 0, -10});
    r.clientCar = &mine.body;
    r.client.world.add(&mine.body);
    r.run(1.0);
    r.client.world.remove(&mine.body);
    EXPECT_FALSE(r.client.set.standing(0)); // predicted
    r.run(2.5);
    EXPECT_TRUE(r.client.set.standing(0));
    EXPECT_TRUE(piecesOf(r.client.set, 0).empty());
    EXPECT_EQ(r.propClient->stats().undone, 1u);
    EXPECT_TRUE(r.host.set.standing(0));
}

// On a client only its own car touches props: the other cars (kinematic
// network cars, the shared traffic) pass through them, since the host
// decides what they do.
TEST(PropSync, OtherCarsPassThroughAClientsProps) {
    Race r;
    Car other({0, 1, 3.0f}, {0, 0, -10});
    r.client.world.add(&other.body);
    r.run(1.5);
    EXPECT_TRUE(r.client.set.standing(0));
    EXPECT_NEAR(other.body.ics.linearVelocity.z, -10.0f, 1e-3f); // not slowed
    EXPECT_EQ(r.propClient->stats().predicted, 0u);
    r.client.world.remove(&other.body);
}

// Lost, late and reordered messages: the client still ends with the host's
// props, and every knock is applied once.
// A client replays its car's samples when the host's state corrects it
// (World::replaySample): the props it meets there hold still, but with the
// mass a real hit gives them, so the replayed car loses what the real one
// does instead of stopping as against a wall.
TEST(PropSync, ReplayedCarMeetsAPropAsARealSampleDoes) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    constexpr int kSteps = 20;
    Machine real(lib);
    real.place(layout());
    Car a({0, 1, 2.5f}, {0, 0, -10});
    real.world.add(&a.body);
    for (int i = 0; i < kSteps; ++i)
        real.step();
    ASSERT_FALSE(real.set.standing(0)); // the real car broke it loose
    Machine replay(lib);
    replay.place(layout());
    Car b({0, 1, 2.5f}, {0, 0, -10});
    replay.world.add(&b.body);
    phys::Body* bodies[] = {&b.body};
    replay.world.beginReplay();
    for (int i = 0; i < kSteps; ++i)
        replay.world.replaySample(bodies, kDt);
    EXPECT_TRUE(replay.set.standing(0)); // a replay knocks nothing
    const float realSpeed = a.body.ics.linearVelocity.mag();
    const float replayedSpeed = b.body.ics.linearVelocity.mag();
    EXPECT_GT(realSpeed, 5.0f);
    EXPECT_NEAR(replayedSpeed, realSpeed, 1.0f) << "real " << realSpeed << ", replayed " << replayedSpeed;
    real.world.remove(&a.body);
    replay.world.remove(&b.body);
}

TEST(PropSync, LossyLinkConverges) {
    Race r;
    r.net.latencyMs = 120.0;
    r.net.jitterMs = 60.0; // reorders
    r.net.loss = 0.3;
    Car car({0, 1, 3.0f}, {2.4f, 0, -10}); // across the row
    r.host.world.add(&car.body);
    r.run(1.2);
    r.host.world.remove(&car.body);
    r.run(6.0);
    std::size_t knocked = 0;
    for (std::size_t i = 0; i < placedProps(r.host.set); ++i) {
        EXPECT_EQ(r.host.set.standing(i), r.client.set.standing(i)) << i;
        if (!r.host.set.standing(i)) {
            ++knocked;
            expectSamePieces(r, i, 0.01f);
        }
    }
    EXPECT_GE(knocked, 1u);
    EXPECT_EQ(r.propClient->stats().knocks, knocked);
    EXPECT_EQ(r.propClient->stats().refused, 0u);
}

// A big crash: a car ploughs through a field of 60 props. Every message
// stays one datagram; the ring (40, MM2's dgBangerManager) bounds it, the
// oldest knocked-over props disappearing as it wraps, on every machine alike.
TEST(PropSync, ABigCrashStaysWithinADatagram) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine host(lib), client(lib);
    std::vector<PlacedProp> field;
    for (int i = 0; i < 60; ++i)
        field.push_back({"light", Mat34::translation({-1.5f + static_cast<float>(i % 3) * 1.5f, 0.0f,
                                                      -static_cast<float>(i / 3) * 2.0f}),
                         1, PlacedProp::Source::Instance, true});
    host.place(field);
    client.place(field);
    client.set.setReplica([](const phys::Instance&) { return false; });
    PropHost propHost;
    PropClient propClient(propCatalog(client.set));
    Car car({0, 1, 4.0f}, {0, 0, -25});
    host.world.add(&car.body);
    std::size_t largest = 0, bytes = 0, events = 0, messages = 0;
    double now = 10000.0;
    for (int i = 0; i < 8 * 60; ++i) {
        if (i == 5 * 60)
            host.world.remove(&car.body);
        now += kStepMs;
        host.step();
        const auto t = static_cast<std::uint32_t>(now);
        propHost.knocked(host.set.takeKnocks(), t);
        for (auto& e : propHost.takeKnockEvents()) {
            EXPECT_LE(e.size(), net::kMaxEventPayload);
            ++events;
            net::PropKnocksEvent k;
            ASSERT_TRUE(net::decodePayload(e, k));
            propClient.receiveKnocks(k);
        }
        if (auto m = propHost.build(host.set, t, static_cast<std::uint64_t>(now), propCatalog(host.set))) {
            const auto encoded = net::encodeMessage(*m);
            largest = std::max(largest, encoded.size());
            bytes += encoded.size();
            ++messages;
            propClient.receive(*m, now + 50.0);
        }
        propClient.update(client.set, now, nullptr);
        client.step();
    }
    std::size_t knocked = 0;
    for (std::size_t i = 0; i < 60; ++i) {
        knocked += host.set.standing(i) ? 0 : 1;
        EXPECT_EQ(client.set.standing(i), host.set.standing(i)) << i;
    }
    EXPECT_GT(knocked, 20u);
    EXPECT_LT(largest, 1100u);
    // About 20 messages a second while the props fly, far fewer once they rest.
    EXPECT_LT(bytes / 8, 12000u); // bytes a second, on average over the crash
    std::printf("big crash: %zu props knocked, %zu messages (largest %zu bytes, %zu bytes/s), %zu knock "
                "events\n",
                knocked, messages, largest, bytes / 8, events);
}

// A machine that loads the race late (or a prop knocked far from it) gets
// every knock so far at once: PropHost::catchUp.
TEST(PropSync, LateMachineCatchesUp) {
    Race r;
    Car car({0, 1, 3.0f}, {2.4f, 0, -10});
    r.host.world.add(&car.body);
    r.run(1.0);
    r.host.world.remove(&car.body);
    r.run(3.0);
    Machine late(r.lib);
    late.place(layout());
    PropClient client(propCatalog(late.set));
    late.set.setReplica([](const phys::Instance&) { return false; });
    for (const auto& payload : PropHost::catchUp(r.host.set, static_cast<std::uint32_t>(r.now))) {
        net::PropKnocksEvent e;
        ASSERT_TRUE(net::decodePayload(payload, e));
        EXPECT_TRUE(e.catchUp);
        client.receiveKnocks(e);
    }
    client.update(late.set, r.now, nullptr);
    for (std::size_t i = 0; i < placedProps(r.host.set); ++i)
        EXPECT_EQ(late.set.standing(i), r.host.set.standing(i)) << i;
}

// A part thrown off a car is a host prop like the others: the client draws
// it from the car's model, and the client's own car's part, thrown at once,
// hands over to the host's.
TEST(PropSync, ThrownCarPartsFollowTheHost) {
    Race r;
    const auto* data = r.lib.find("car_break0");
    ASSERT_TRUE(data);
    const std::uint32_t tag = carPartTag(net::PropOwner::Player, 1, 0, 2);
    int resolved = 0;
    r.resolver = [&](const net::PropDescriptor& d) -> std::optional<BangerSet::MirrorSpec> {
        ++resolved;
        if (d.source != net::PropSource::CarPart || d.ownerId != 1 || d.part != 0)
            return std::nullopt;
        BangerSet::MirrorSpec spec;
        spec.data = data;
        spec.model = "car";
        spec.mesh = "BREAK0";
        spec.paint = d.paint;
        spec.tag = carPartTag(d.owner, d.ownerId, d.part, d.paint);
        return spec;
    };
    const Mat34 at = Mat34::translation({50, 1, 0});
    r.client.set.ejectPart(*data, "car", "BREAK0", 2, at, 4.0f, 1, tag); // the client's own, at once
    r.run(0.1);
    r.host.set.ejectPart(*data, "car", "BREAK0", 2, at, 4.0f, 1, tag); // the host's
    r.run(6.0);
    EXPECT_GT(resolved, 0);
    std::vector<Vec3> hostParts, clientParts;
    for (const auto& inst : r.host.set.instances())
        if (inst.tag == tag && inst.state != BangerSet::State::Gone)
            hostParts.push_back(inst.matrix.m3);
    for (const auto& inst : r.client.set.instances())
        if (inst.tag == tag && inst.state != BangerSet::State::Gone) {
            EXPECT_TRUE(inst.mirror);
            EXPECT_EQ(inst.mesh, "BREAK0");
            clientParts.push_back(inst.drawn.value_or(inst.matrix).m3);
        }
    ASSERT_EQ(hostParts.size(), 1u);
    ASSERT_EQ(clientParts.size(), 1u);
    EXPECT_LT(hostParts[0].dist(clientParts[0]), 0.01f);
}

// What a dishonest or different host sends: another placement (only the cars'
// parts are followed, the client simulates its props itself), props it does
// not have, a slot that changes what it holds within a generation.
TEST(PropSync, RefusesWhatItCannotFollow) {
    Race r;
    net::PropStateMsg m;
    m.time = 10100;
    m.catalog = propCatalog(r.client.set);
    net::PropSlot p;
    p.slot = 0;
    p.generation = 1;
    p.what = {net::PropSource::Prop, 30000, 0, net::PropOwner::Player, 0, 0}; // not placed here
    p.position = {0, 1, 0};
    m.slots.push_back(p);
    p.slot = 1;
    p.what.prop = 4;
    m.slots.push_back(p);
    p.slot = 1; // the same slot again
    m.slots.push_back(p);
    r.propClient->receive(m, 10100);
    EXPECT_EQ(r.propClient->stats().refused, 1u);
    m.time = 10200;
    m.slots.resize(2);
    m.slots[1].what.prop = 3; // slot 1, generation 1 held prop 4
    r.propClient->receive(m, 10200);
    EXPECT_EQ(r.propClient->stats().refused, 2u);
    net::PropKnocksEvent e;
    e.time = 10000;
    e.knocks = {{30000, 0}, {4, 0}};
    r.propClient->receiveKnocks(e);
    r.propClient->update(r.client.set, 11000, nullptr);
    EXPECT_TRUE(r.client.set.standing(0));
    EXPECT_FALSE(r.client.set.standing(4)); // the honest knock
    EXPECT_EQ(r.propClient->stats().knocks, 1u);

    // A host with other props: the client follows none of them and goes
    // back to simulating every contact itself.
    m.catalog ^= 1u;
    m.time = 10300;
    r.propClient->receive(m, 10300);
    EXPECT_TRUE(r.propClient->catalogMismatch());
    r.propClient->update(r.client.set, 11100, nullptr);
    EXPECT_FALSE(r.client.set.replica());
    e.knocks = {{1, 0}};
    r.propClient->receiveKnocks(e);
    r.propClient->update(r.client.set, 11200, nullptr);
    EXPECT_TRUE(r.client.set.standing(1));
}

namespace {

struct Placement {
    std::uint32_t catalog = 0;
    std::size_t placed = 0;
};

// One machine's props in a network race, in RaceScreen's order: the city's
// (loadWorldObjects), the gizmos' parked cars (initGizmos, multiplayer), the
// traffic lights (addTrafficLightProps, where aiMap::Init runs).
Placement placeAsAMachine(const vfs::Vfs& v, const char* name, GameMode mode, int raceIndex, float traffic) {
    auto city = city::loadCity(v, name);
    if (!city)
        return {};
    bangers::BangerDataLibrary data(v);
    CityLevel level(*city, v, [&](std::string_view n) { return data.has(n); });
    phys::World physics(level.takeMaterials());
    physics.setLevel(&level);
    BangerSet set(data);
    std::uint32_t state = 1;
    set.add(bangers::placeCityProps(*city, v, data, bangers::racePropsName(mode, raceIndex), &state));
    ai::Random random(state);
    takeVehCarInitDraws(random);
    RaceConfig config;
    config.city = name;
    config.mode = mode;
    config.raceIndex = raceIndex;
    auto gizmos = world::initGizmos(v, *city, config, true, set, data, &level, random);
    if (mode == GameMode::Cruise) {
        ai::Settings settings;
        settings.trafficDensity = traffic;
        settings.pedestrianDensity = 0.0f;
        settings.random = &random;
        const auto ai = ai::World::create(*city, v, settings);
        if (!ai)
            return {};
        for (const ai::Signal& s : ai->signals()) {
            PlacedProp p;
            p.model = s.model;
            p.transform = s.transform;
            p.room = level.findRoom(s.position(), 0);
            p.ownerDrawn = true;
            set.addOne(p);
        }
    }
    set.setWorld(&physics);
    Placement out{propCatalog(set), placedProps(set)};
    set.setWorld(nullptr);
    return out;
}

} // namespace

// A client takes knocks from the host only: another player's events reach it
// through the host's relay, and a forged one knocks nothing.
TEST(PropSync, KnocksCountFromTheHostOnly) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine client(lib);
    client.place(layout());
    NetGame net(NetOptions{}); // in no session: a client's side
    NetProps props;
    props.setup(net, client.set, [](const phys::Instance&) { return false; });
    ASSERT_TRUE(props.client());
    net::PropKnocksEvent knocks;
    knocks.catchUp = true;
    knocks.knocks = {{1, 0}};
    NetGameEvent forged;
    forged.from = 2;
    forged.type = static_cast<net::GameEventType>(net::kPropKnocksEvent);
    forged.payload = net::encodePayload(knocks);
    props.beforeStep(net, std::span(&forged, 1), 1000.0, nullptr);
    EXPECT_TRUE(client.set.standing(1));
    NetGameEvent honest = forged;
    honest.from = net::kHostPlayerId;
    props.beforeStep(net, std::span(&honest, 1), 1100.0, nullptr);
    EXPECT_FALSE(client.set.standing(1));
}

// Retail data: every machine of a network race places the same props with
// the same indices. The street props restart the random generator per road,
// the parked cars (network races only) draw from the stream they leave, and
// the traffic lights come from the AI map whatever the traffic density (the
// host runs the shared traffic, a client none).
TEST(PropSyncRetail, EveryMachinePlacesTheSameProps) {
    MM2_REQUIRE_GAME_DATA();
    const auto& v = *test::gameData();
    auto place = [&](const char* name, GameMode mode, int raceIndex, float traffic) {
        return placeAsAMachine(v, name, mode, raceIndex, traffic);
    };
    for (const char* name : {"sf", "london"}) {
        // Cruise: the host at the cruise menu's traffic, a client at none.
        const Placement host = place(name, GameMode::Cruise, -1, 0.5f);
        const Placement client = place(name, GameMode::Cruise, -1, 0.0f);
        ASSERT_GT(host.placed, 1000u) << name;
        EXPECT_LT(host.placed, net::kMaxPropIds) << name;
        EXPECT_EQ(host.placed, client.placed) << name;
        EXPECT_EQ(host.catalog, client.catalog) << name;
        // A checkpoint race: parked cars, no traffic lights.
        const Placement a = place(name, GameMode::Checkpoint, 0, 0.0f);
        const Placement b = place(name, GameMode::Checkpoint, 0, 0.0f);
        EXPECT_EQ(a.placed, b.placed) << name;
        EXPECT_EQ(a.catalog, b.catalog) << name;
        EXPECT_NE(a.catalog, host.catalog) << name;
    }
}
