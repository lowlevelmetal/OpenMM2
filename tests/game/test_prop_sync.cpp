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

#include <algorithm>
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

// What the race screen keeps for a client's replays (RaceScreen bodyPoses /
// placeBodies): where the bodies around its car stood for each sample, put
// back there as that sample runs again, and where they stand now after.
struct StoodPose {
    phys::Body* body;
    Mat34 ics, bound;
    Vec3 velocity, spin;
};

class BodyHistory {
public:
    // Before a real sample.
    void record(const phys::World& world, const phys::Body& car) {
        std::vector<phys::Body*> near;
        world.bodiesNear(car.ics.matrix.m3, 40.0f, near);
        std::vector<StoodPose> poses;
        for (phys::Body* b : near)
            if (b != &car)
                poses.push_back(poseOf(*b));
        m_samples.push_back(std::move(poses));
    }
    // A replay of the recorded samples, as reconcileNetCar runs it.
    void replay(phys::World& world, phys::Body& car, double from) {
        std::vector<StoodPose> now;
        for (const auto& poses : m_samples)
            for (const auto& p : poses)
                if (std::ranges::none_of(now, [&](const StoodPose& q) { return q.body == p.body; }) &&
                    world.contains(p.body))
                    now.push_back(poseOf(*p.body));
        phys::Body* bodies[] = {&car};
        world.beginReplay(from);
        for (const auto& poses : m_samples) {
            place(world, poses);
            world.replaySample(bodies, kDt);
        }
        place(world, now);
    }

private:
    static StoodPose poseOf(phys::Body& b) {
        return {&b, b.ics.matrix, b.boundMatrix, b.ics.linearVelocity, b.ics.angularVelocity};
    }
    static void place(const phys::World& world, const std::vector<StoodPose>& poses) {
        for (const auto& p : poses) {
            if (!world.contains(p.body))
                continue;
            p.body->ics.matrix = p.ics;
            p.body->boundMatrix = p.bound;
            p.body->ics.linearVelocity = p.velocity;
            p.body->ics.angularVelocity = p.spin;
        }
    }
    std::vector<std::vector<StoodPose>> m_samples;
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
    const phys::Instance* nearCar = nullptr; // another player's, simulated on the client too
    const phys::Body* hostNearCar = nullptr; // ... and the host's (its states reach the client)
    const phys::Body* clientBody = nullptr;  // the client's own car, where the client's props see it

    Race() {
        host.place(layout());
        client.place(layout());
        propClient.emplace(propCatalog(client.set));
        client.set.setReplica(
            [this](const phys::Instance& other) { return &other == clientCar || &other == nearCar; });
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
        propClient->update(client.set, now, resolver, [this](int car) {
            // The host's state of the near car, a link's latency ago.
            using State = std::optional<std::pair<double, Vec3>>;
            if (car != 2 || !hostNearCar)
                return State{};
            return State{{now - net.latencyMs, hostNearCar->ics.matrix.m3}};
        }, clientBody ? std::optional{clientBody->ics.matrix.m3} : std::nullopt);
        client.step();
        propClient->predicted(client.set.takeKnocks(), now, 1, [this](const phys::Instance* by) {
            return by == clientCar ? 1 : by == nearCar ? 2 : -1;
        });
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
// undone: the prop stands again on the client, as soon as the host's
// messages have passed the knock's time by more than the host's lag.
TEST(PropSync, UnconfirmedKnockIsUndone) {
    Race r;
    Car mine({0, 1, 3.0f}, {0, 0, -10});
    r.clientCar = &mine.body;
    r.client.world.add(&mine.body);
    while (r.client.set.standing(0) && r.now < 12000.0)
        r.frame();
    const double knockedAt = r.now;
    ASSERT_FALSE(r.client.set.standing(0)); // predicted
    r.run(0.4);
    r.client.world.remove(&mine.body);
    EXPECT_FALSE(r.client.set.standing(0)); // still: the host may yet knock it
    while (!r.client.set.standing(0) && r.now < knockedAt + 3000.0)
        r.frame();
    EXPECT_TRUE(r.client.set.standing(0));
    // The host's next message after it (2 a second with nothing moving)
    // passed it, well before the 2 s.
    EXPECT_LT(r.now - knockedAt, 1300.0);
    EXPECT_TRUE(piecesOf(r.client.set, 0).empty());
    EXPECT_EQ(r.propClient->stats().undone, 1u);
    EXPECT_EQ(r.propClient->stats().undoneEarly, 1u);
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
// A client that replays its car through the sample where its car knocked a
// prop (the host's state came from before the knock) meets the prop where it
// stood, not knocked and gone: the replay takes the same knock and ends where
// the real samples did.
TEST(PropSync, ReplayThroughAKnockTakesTheSameKnock) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine client(lib);
    client.place(layout());
    Car car({0, 1, 4.5f}, {0, 0, -10});
    client.set.setReplica([&car](const phys::Instance& other) { return &other == &car.body; });
    client.world.add(&car.body);
    // The state a correction would go back to: before the hit.
    for (int i = 0; i < 5; ++i)
        client.step();
    ASSERT_TRUE(client.set.standing(0));
    const Mat34 matrix = car.body.ics.matrix;
    const Vec3 velocity = car.body.ics.linearVelocity;
    const double from = client.world.time();
    BodyHistory history;
    for (int i = 0; i < 25; ++i) {
        history.record(client.world, car.body);
        client.step();
    }
    ASSERT_FALSE(client.set.standing(0)); // the real samples knocked it
    const float real = car.body.ics.linearVelocity.z;
    const Vec3 realAt = car.body.ics.matrix.m3;
    // Back to the earlier state, and the same samples again as a replay.
    car.body.place(matrix);
    car.body.ics.linearVelocity = velocity;
    car.body.ics.linearMomentum = velocity * car.body.ics.mass;
    history.replay(client.world, car.body, from);
    EXPECT_FALSE(client.set.standing(0)); // a replay changes no prop
    EXPECT_NEAR(car.body.ics.linearVelocity.z, real, 0.2f) << "real " << real;
    EXPECT_LT(car.body.ics.matrix.m3.dist(realAt), 0.1f);
    EXPECT_GT(car.body.ics.linearVelocity.z, -9.9f); // it met the prop
    client.world.remove(&car.body);
}

// A replay that starts after the knock, the car still across the prop's
// place or past it, ends where the real samples did: it meets the pieces
// where they were, not the prop where it stood (which would knock the car a
// second time).
TEST(PropSync, ReplayAfterAKnockEndsAsTheRealSamples) {
    for (const int after : {1, 4, 8}) {
        TempBangers files;
        bangers::BangerDataLibrary lib(files.vfs);
        Machine client(lib);
        client.place(layout());
        Car car({0, 1, 4.5f}, {0, 0, -10});
        client.set.setReplica([&car](const phys::Instance& other) { return &other == &car.body; });
        client.world.add(&car.body);
        while (client.set.standing(0))
            client.step();
        for (int i = 0; i < after; ++i)
            client.step();
        const Mat34 matrix = car.body.ics.matrix;
        const Vec3 velocity = car.body.ics.linearVelocity;
        const double from = client.world.time();
        BodyHistory history;
        for (int i = 0; i < 15; ++i) {
            history.record(client.world, car.body);
            client.step();
        }
        const float real = car.body.ics.linearVelocity.z;
        const Vec3 realAt = car.body.ics.matrix.m3;
        car.body.place(matrix);
        car.body.ics.linearVelocity = velocity;
        car.body.ics.linearMomentum = velocity * car.body.ics.mass;
        history.replay(client.world, car.body, from);
        EXPECT_NEAR(car.body.ics.linearVelocity.z, real, 0.06f) << after << " samples after, real " << real;
        EXPECT_LT(car.body.ics.matrix.m3.dist(realAt), 0.03f) << after << " samples after";
        client.world.remove(&car.body);
    }
}

// A replay knocks and moves no prop for real: the standing props stand, the
// pieces and the host's mirrors stay where they are, with their motion.
TEST(PropSync, AReplayMovesNoPropForReal) {
    Race r;
    Car host({0, 1, 3.0f}, {0, 0, -10}); // the host knocks prop 0
    r.host.world.add(&host.body);
    Car mine({6, 1, 4.5f}, {0, 0, -10}); // the client's own car knocks prop 1
    r.clientCar = &mine.body;
    r.client.world.add(&mine.body);
    const double from = r.client.world.time(); // the replay below meets prop 1 standing
    r.run(0.6);
    r.host.world.remove(&host.body);
    ASSERT_FALSE(r.client.set.standing(1));
    struct Seen {
        BangerSet::State state;
        Vec3 at;
        int active;
    };
    auto seen = [&] {
        std::vector<Seen> out;
        for (std::size_t i = 0; i < r.client.set.instances().size(); ++i) {
            const auto& inst = r.client.set.instances()[i];
            out.push_back({inst.state, inst.matrix.m3, inst.active});
        }
        return out;
    };
    const auto before = seen();
    std::vector<Vec3> bodies;
    for (std::size_t i = 0; i < r.client.set.instances().size(); ++i)
        if (const phys::Body* b = r.client.set.body(i))
            bodies.push_back(b->ics.linearVelocity);
    // The car run back through the props, as a correction would.
    Car again({0, 1, 4.5f}, {3, 0, -12});
    r.client.world.add(&again.body);
    phys::Body* replayed[] = {&again.body};
    r.client.world.beginReplay(from);
    for (int i = 0; i < 40; ++i)
        r.client.world.replaySample(replayed, kDt);
    r.client.world.remove(&again.body);
    const auto after = seen();
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].state, after[i].state) << i;
        EXPECT_EQ(before[i].active, after[i].active) << i;
        EXPECT_LT(before[i].at.dist(after[i].at), 1e-6f) << i;
    }
    std::size_t k = 0;
    for (std::size_t i = 0; i < r.client.set.instances().size(); ++i) {
        if (const phys::Body* b = r.client.set.body(i)) {
            EXPECT_LT(b->ics.linearVelocity.dist(bodies[k++]), 1e-6f) << i;
        }
    }
    EXPECT_TRUE(r.client.set.standing(2));
    EXPECT_TRUE(r.client.set.standing(3));
}

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
    replay.world.beginReplay(replay.world.time());
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

// A piece a client's own car knocked flies into the prop behind: in single
// player it knocks that one too; on a client it passes through (the host's
// piece of the same knock flies differently; the host's knocks bring the
// ones it makes).
TEST(PropSync, APredictedPieceKnocksNoPlacedProp) {
    for (const bool replica : {false, true}) {
        TempBangers files;
        bangers::BangerDataLibrary lib(files.vfs);
        Machine m(lib);
        m.place({{"light", Mat34::translation({0, 0, 0}), 1, PlacedProp::Source::Instance, true},
                 {"light", Mat34::translation({0, 0, -1.2f}), 1, PlacedProp::Source::Instance, true}});
        Car car({0, 1, 3.0f}, {0, 0, -25});
        if (replica)
            m.set.setReplica([&car](const phys::Instance& other) { return &other == &car.body; });
        m.world.add(&car.body);
        while (m.set.standing(0) && m.world.time() < 1.0)
            m.step();
        m.world.remove(&car.body); // only the piece goes on
        ASSERT_FALSE(m.set.standing(0)) << replica;
        for (int i = 0; i < 60; ++i)
            m.step();
        EXPECT_EQ(m.set.standing(1), replica) << "the piece knocked the prop behind";
    }
}

// A pile-up of more knocks than MM2's ring of 40 holds: in a network race
// the ring grows rather than make the props knocked moments ago disappear
// (BangerSet::setRingGrowth); a single-player ring wraps as MM2's does.
TEST(PropSync, APileUpGrowsTheRing) {
    for (const bool network : {false, true}) {
        TempBangers files;
        bangers::BangerDataLibrary lib(files.vfs);
        Machine m(lib);
        std::vector<PlacedProp> column;
        for (int i = 0; i < 60; ++i)
            column.push_back({"light", Mat34::translation({0.0f, 0.0f, -static_cast<float>(i) * 1.5f}), 1,
                              PlacedProp::Source::Instance, true});
        m.place(column);
        if (network)
            m.set.setRingGrowth(160, 10.0);
        Car car({0, 1, 4.0f}, {0, 0, -12});
        m.world.add(&car.body);
        for (int i = 0; i < 9 * 60; ++i) {
            // Driven straight on at 12 m/s whatever it hits (the 32 actives
            // come free as the first pieces come to rest).
            car.body.place(Mat34::translation({0, 1, car.body.ics.matrix.m3.z}));
            car.body.ics.linearVelocity = {0, 0, -12};
            car.body.ics.linearMomentum = car.body.ics.linearVelocity * car.body.ics.mass;
            car.body.ics.angularVelocity = {};
            car.body.ics.angularMomentum = {};
            m.step();
        }
        m.world.remove(&car.body);
        std::size_t knocked = 0, shown = 0;
        for (std::size_t i = 0; i < 60; ++i)
            knocked += m.set.standing(i) ? 0 : 1;
        for (const std::size_t i : m.set.ring())
            shown += m.set.instances()[i].state != BangerSet::State::Gone ? 1 : 0;
        EXPECT_GT(knocked, 45u) << network;
        if (network) {
            EXPECT_EQ(m.set.ringSize(), 80);
            EXPECT_EQ(shown, knocked); // every knocked prop still there
        } else {
            EXPECT_EQ(m.set.ringSize(), BangerSet::kMaxHit);
            EXPECT_EQ(shown, static_cast<std::size_t>(BangerSet::kMaxHit));
        }
    }
}

// Two clients far apart, each with props flying around its car: each one's
// messages carry the states near its car every time, within the datagram
// budget, and the far ones as room allows; both end with every piece where
// the host's rests.
TEST(PropSync, EachClientGetsItsAreaFirst) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine host(lib), a(lib), b(lib);
    std::vector<PlacedProp> field;
    for (const float x : {0.0f, 300.0f})
        for (int i = 0; i < 10; ++i)
            field.push_back({"light", Mat34::translation({x, 0.0f, -static_cast<float>(i) * 1.5f}), 1,
                             PlacedProp::Source::Instance, true});
    host.place(field);
    a.place(field);
    b.place(field);
    a.set.setReplica([](const phys::Instance&) { return false; });
    b.set.setReplica([](const phys::Instance&) { return false; });
    PropHost::Options options;
    options.maxBytes = 300;
    PropHost propHost(options);
    PropClient clientA(propCatalog(a.set)), clientB(propCatalog(b.set));
    Car carA({0, 1, 4.0f}, {0, 0, -20}), carB({300, 1, 4.0f}, {0, 0, -20});
    host.world.add(&carA.body);
    host.world.add(&carB.body);
    const PropHost::Viewer viewers[] = {{1, Vec3{0, 1, -5}}, {2, Vec3{300, 1, -5}}};
    double now = 10000.0;
    std::size_t nearMoving = 0, nearMovingSent = 0, farSent = 0, largest = 0;
    for (int i = 0; i < 10 * 60; ++i) {
        if (i == 3 * 60) {
            host.world.remove(&carA.body);
            host.world.remove(&carB.body);
        }
        now += kStepMs;
        host.step();
        const auto t = static_cast<std::uint32_t>(now);
        propHost.knocked(host.set.takeKnocks(), t);
        for (auto& e : propHost.takeKnockEvents()) {
            net::PropKnocksEvent k;
            ASSERT_TRUE(net::decodePayload(e, k));
            clientA.receiveKnocks(k);
            clientB.receiveKnocks(k);
        }
        const auto msgs =
            propHost.build(host.set, t, static_cast<std::uint64_t>(now), propCatalog(host.set), viewers);
        for (const auto& [id, msg] : msgs) {
            const auto bytes = net::encodeMessage(msg);
            largest = std::max(largest, bytes.size());
            net::PropStateMsg back;
            ASSERT_TRUE(net::decodeMessage(bytes, back));
            const Vec3 at = *viewers[id - 1].at;
            for (const auto& p : back.slots) {
                const std::size_t inst = host.set.ring()[p.slot];
                const bool near = host.set.instances()[inst].matrix.m3.dist(at) < options.nearRadius;
                if (near && host.set.moving(inst)) {
                    ++nearMoving;
                    nearMovingSent += p.hasState ? 1 : 0;
                } else if (!near && p.hasState) {
                    ++farSent;
                }
            }
            (id == 1 ? clientA : clientB).receive(back, now + 40.0);
        }
        clientA.update(a.set, now, nullptr);
        clientB.update(b.set, now, nullptr);
        a.step();
        b.step();
    }
    EXPECT_LE(largest, options.maxBytes);
    EXPECT_GT(nearMoving, 100u);
    EXPECT_EQ(nearMovingSent, nearMoving); // every one, every time
    EXPECT_GT(farSent, 0u);
    EXPECT_GT(propHost.stats().deferred, 0u); // the budget held some far ones back
    std::size_t knocked = 0;
    for (std::size_t prop = 0; prop < field.size(); ++prop) {
        if (host.set.standing(prop))
            continue;
        ++knocked;
        const Vec3 rest = piecesOf(host.set, prop).at(-1);
        for (const BangerSet* client : {&a.set, &b.set}) {
            EXPECT_FALSE(client->standing(prop)) << prop;
            const auto shown = piecesOf(*client, prop);
            ASSERT_TRUE(shown.contains(-1)) << prop;
            EXPECT_LT(shown.at(-1).dist(rest), 0.01f) << prop;
        }
    }
    EXPECT_GT(knocked, 10u);
    std::printf("areas: %zu props knocked, near moving states %zu of %zu, far states %zu, %llu deferred, "
                "largest %zu bytes\n",
                knocked, nearMovingSent, nearMoving, farSent,
                static_cast<unsigned long long>(propHost.stats().deferred), largest);
}

// More pieces flying near a client than a message's datagram holds: they
// take turns, none waiting long for its state.
TEST(PropSync, APileUpBeyondADatagramTakesTurns) {
    TempBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    Machine host(lib);
    std::vector<PlacedProp> field;
    for (int i = 0; i < 24; ++i)
        field.push_back({"light", Mat34::translation({-1.5f + static_cast<float>(i % 3) * 1.5f, 0.0f,
                                                      -static_cast<float>(i / 3) * 2.0f}),
                         1, PlacedProp::Source::Instance, true});
    host.place(field);
    PropHost::Options options;
    options.maxBytes = 200; // a few flying states a message
    PropHost propHost(options);
    Car car({0, 1, 4.0f}, {0, 0, -25});
    host.world.add(&car.body);
    const PropHost::Viewer viewer[] = {{1, Vec3{0, 1, -8}}};
    std::map<std::size_t, int> lastState; // slot -> message
    int message = 0, longest = 0, flying = 0;
    double now = 10000.0;
    for (int i = 0; i < 3 * 60; ++i) {
        now += kStepMs;
        host.step();
        host.set.takeKnocks();
        const auto msgs = propHost.build(host.set, static_cast<std::uint32_t>(now),
                                         static_cast<std::uint64_t>(now), propCatalog(host.set), viewer);
        for (const auto& [id, msg] : msgs) {
            EXPECT_LE(net::encodeMessage(msg).size(), options.maxBytes);
            ++message;
            for (const auto& p : msg.slots) {
                const std::size_t inst = host.set.ring()[p.slot];
                if (!host.set.moving(inst)) {
                    lastState.erase(p.slot);
                    continue;
                }
                ++flying;
                if (p.hasState) {
                    lastState[p.slot] = message;
                } else {
                    const auto it = lastState.try_emplace(p.slot, message).first;
                    longest = std::max(longest, message - it->second);
                }
            }
        }
    }
    EXPECT_GT(propHost.stats().deferred, 0u);
    EXPECT_GT(flying, 200);
    EXPECT_LE(longest, 8); // messages a flying piece waited at most for its state
    std::printf("pile-up: %d flying slots listed, %llu states deferred, longest wait %d messages\n", flying,
                static_cast<unsigned long long>(propHost.stats().deferred), longest);
}

// A client predicts the knocks of the other players' cars it simulates with
// its own (net::NearCarState) as it does its own car's: at once, then the
// host's knock confirms it and its pieces take over.
TEST(PropSync, ANearCarsKnockIsPredicted) {
    Race r;
    Car near({6, 1, 3.0f}, {0, 0, -10}); // another player's car as the client simulates it
    r.nearCar = &near.body;
    r.client.world.add(&near.body);
    Car copy({6, 1, 3.0f}, {0, 0, -10}); // the host's, which knocks the same prop
    r.host.world.add(&copy.body);
    double hostAt = 0, clientAt = 0;
    for (int i = 0; i < 7 * 60; ++i) {
        r.frame();
        if (hostAt == 0 && !r.host.set.standing(1))
            hostAt = r.now;
        if (clientAt == 0 && !r.client.set.standing(1))
            clientAt = r.now;
        if (i == 60) {
            r.client.world.remove(&near.body);
            r.host.world.remove(&copy.body);
        }
    }
    ASSERT_GT(hostAt, 0);
    EXPECT_LE(clientAt, hostAt); // as it met the car here, not a delay later
    const auto& s = r.propClient->stats();
    EXPECT_EQ(s.predicted, 1u);
    EXPECT_EQ(s.confirmed, 1u);
    EXPECT_EQ(s.undone, 0u);
    expectSamePieces(r, 1, 0.01f);
}

// One the host's car did not make (its player turned away) stands again,
// once the host's states had that car away from the prop.
TEST(PropSync, ANearCarsMissedKnockIsUndone) {
    Race r;
    Car near({6, 1, 3.0f}, {0, 0, -10});
    r.nearCar = &near.body;
    r.client.world.add(&near.body);
    Car copy({40, 1, 3.0f}, {0, 0, -10}); // elsewhere on the host
    r.hostNearCar = &copy.body;
    r.host.world.add(&copy.body);
    r.run(0.5);
    r.client.world.remove(&near.body);
    EXPECT_FALSE(r.client.set.standing(1)); // predicted
    r.run(1.0);
    EXPECT_TRUE(r.client.set.standing(1)); // before the 2 s
    EXPECT_TRUE(r.host.set.standing(1));
    EXPECT_EQ(r.propClient->stats().undone, 1u);
    EXPECT_EQ(r.propClient->stats().undoneEarly, 1u);
    r.host.world.remove(&copy.body);
}

// One the host's near car did not make while this machine's own car is on
// its way to the prop: it stands (the host's knock comes from the client's
// car), rather than standing up and falling again a moment later.
TEST(PropSync, ANearCarsKnockStandsWhileThisCarIsComing) {
    Race r;
    Car near({6, 1, 3.0f}, {0, 0, -10});
    r.nearCar = &near.body;
    r.client.world.add(&near.body);
    Car away({40, 1, 3.0f}, {0, 0, -10}); // the near car elsewhere on the host
    r.hostNearCar = &away.body;
    r.host.world.add(&away.body);
    Car mine({6, 1, 12.0f}, {0, 0, -8}); // the client's own car, 1.5 s from the prop
    r.clientCar = &mine.body;
    r.clientBody = &mine.body;
    r.client.world.add(&mine.body);
    Car copy({6, 1, 12.0f}, {0, 0, -8}); // and the host's copy of it
    r.host.world.add(&copy.body);
    r.run(0.4);
    r.client.world.remove(&near.body);
    ASSERT_FALSE(r.client.set.standing(1)); // the near car's knock, predicted
    for (int i = 0; i < 3 * 60; ++i) {
        r.frame();
        EXPECT_FALSE(r.client.set.standing(1)) << i; // never up again
    }
    EXPECT_FALSE(r.host.set.standing(1)); // the client's car knocked it there
    EXPECT_EQ(r.propClient->stats().undone, 0u);
    EXPECT_EQ(r.propClient->stats().confirmed, 1u);
    r.client.world.remove(&mine.body);
    r.host.world.remove(&copy.body);
    r.host.world.remove(&away.body);
}

// A near car this machine runs ahead reaches a prop well before the real
// one (its player braked short of it): while the host has that car near the
// prop the knock stands, past the 2 s, and the host's comes in time.
TEST(PropSync, ANearCarsEarlyKnockWaitsForTheHost) {
    Race r;
    Car near({6, 1, 3.0f}, {0, 0, -10});
    r.nearCar = &near.body;
    r.client.world.add(&near.body);
    Car copy({6, 1, 5.5f}, {0, 0, 0}); // stopped short of the prop on the host
    r.hostNearCar = &copy.body;
    r.host.world.add(&copy.body);
    r.run(0.5);
    r.client.world.remove(&near.body);
    EXPECT_FALSE(r.client.set.standing(1)); // predicted
    for (int i = 0; i < 150; ++i) {
        r.frame();
        EXPECT_FALSE(r.client.set.standing(1)) << i; // the host's car is still by it
    }
    copy.body.ics.linearVelocity = {0, 0, -10}; // and drives on into it
    copy.body.ics.linearMomentum = copy.body.ics.linearVelocity * copy.body.ics.mass;
    r.run(1.5);
    EXPECT_FALSE(r.host.set.standing(1));
    EXPECT_FALSE(r.client.set.standing(1));
    EXPECT_EQ(r.propClient->stats().confirmed, 1u);
    EXPECT_EQ(r.propClient->stats().undone, 0u);
    r.host.world.remove(&copy.body);
}

// The host sends the pieces round a client's car in full (net::PropFull):
// a moving one with its body as the host's simulation has it, one at rest
// with its exact frame three times, none far from the car; the client puts
// the host's piece shown there to that state (its car's samples run again
// with it, game::CarPrediction::Companion).
TEST(PropSync, PiecesRoundACarComeInFull) {
    Race r;
    Car host({0, 1, 3.0f}, {0, 0, -10}); // knocks prop 0 on the host
    r.host.world.add(&host.body);
    r.run(0.5);
    r.host.world.remove(&host.body);
    ASSERT_FALSE(r.host.set.standing(0));
    ASSERT_FALSE(r.host.set.ring().empty());
    const std::size_t hit = r.host.set.ring()[0];
    ASSERT_TRUE(r.host.set.body(hit)); // flying
    const Vec3 piece = r.host.set.instances()[hit].matrix.m3;
    const auto time = static_cast<std::uint32_t>(r.now);
    EXPECT_TRUE(r.propHost.buildFull(r.host.set, 1, time, piece + Vec3{100, 0, 0}).empty()); // far
    const auto sent = r.propHost.buildFull(r.host.set, 1, time, piece + Vec3{2, 0, 0});
    ASSERT_EQ(sent.size(), 1u);
    const net::PropFullMsg& full = sent[0];
    ASSERT_EQ(full.pieces.size(), 1u);
    const net::PropFullPiece& p = full.pieces[0];
    EXPECT_TRUE(p.moving);
    EXPECT_EQ(p.slot, 0);
    const phys::Body& body = *r.host.set.body(hit);
    EXPECT_EQ(p.body.matrix.m3, body.ics.matrix.m3);
    EXPECT_EQ(p.body.linearMomentum, body.ics.linearMomentum);
    EXPECT_EQ(p.body.angularVelocity, body.ics.angularVelocity);
    // Not again before kFullIntervalMs.
    EXPECT_TRUE(r.propHost.buildFull(r.host.set, 1, time + 20, piece + Vec3{2, 0, 0}).empty());

    // The client: the host's piece is shown there (a playout delay later),
    // then put to the host's state.
    r.propClient->receiveFull(full);
    const auto pieces = r.propClient->fullPieces(r.client.set, time, r.now);
    ASSERT_EQ(pieces.size(), 1u);
    EXPECT_EQ(pieces[0].instance, r.client.set.mirror(0));
    ASSERT_TRUE(r.client.set.setBodyState(pieces[0].instance, pieces[0].state));
    const phys::Body* mirror = r.client.set.body(pieces[0].instance);
    ASSERT_TRUE(mirror);
    EXPECT_EQ(mirror->ics.matrix.m3, p.body.matrix.m3);
    EXPECT_EQ(mirror->ics.linearMomentum, p.body.linearMomentum);
    EXPECT_EQ(mirror->ics.angularMomentum, p.body.angularMomentum);
    r.propClient->forgetFull(time);
    EXPECT_TRUE(r.propClient->fullPieces(r.client.set, time, r.now).empty());

    // At rest on the host: its exact frame, three times, then no more.
    r.run(6.0);
    ASSERT_FALSE(r.host.set.body(hit));
    const Vec3 rest = r.host.set.instances()[hit].matrix.m3;
    int restSent = 0;
    for (std::uint32_t i = 1; i <= 5; ++i) {
        const std::uint32_t later = time + i * PropHost::kFullIntervalMs;
        for (const auto& m : r.propHost.buildFull(r.host.set, 1, later, rest)) {
            ASSERT_EQ(m.pieces.size(), 1u);
            EXPECT_FALSE(m.pieces[0].moving);
            EXPECT_EQ(m.pieces[0].body.matrix.m3, rest);
            ++restSent;
        }
    }
    EXPECT_EQ(restSent, 3);
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

// The host's pieces move on a client only for its own car: a piece the
// client simulates (a prediction) passes through them, so two pieces lying
// together cannot keep each other moving there while the host's rest.
TEST(PropSync, OnlyTheClientsCarMovesTheHostsPieces) {
    Race r;
    Car car({0, 1, 3.0f}, {0, 0, -10});
    r.host.world.add(&car.body);
    r.run(1.0);
    r.host.world.remove(&car.body);
    r.run(4.0);
    const auto shown = piecesOf(r.client.set, 0);
    ASSERT_EQ(shown.size(), 1u);
    std::size_t mirror = 0;
    for (std::size_t i = 0; i < r.client.set.instances().size(); ++i)
        if (r.client.set.instances()[i].mirror && r.client.set.instances()[i].source == 0)
            mirror = i;
    ASSERT_GT(mirror, 0u);
    // A piece of the client's own dropped onto it.
    const auto* data = r.lib.find("light");
    Mat34 above = Mat34::translation(shown.begin()->second + Vec3{0, 1.5f, 0});
    r.client.set.ejectPart(*data, "light", "", 0, above, 0.0f, 1);
    for (int i = 0; i < 90; ++i) {
        r.frame();
        ASSERT_LT(r.client.set.instances()[mirror].active, 0) << i;
    }
    expectSamePieces(r, 0, 0.01f);
}

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
