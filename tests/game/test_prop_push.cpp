// A client's car pushing knocked-over props (OpenMM2, net::PropFull): a host
// and a client in one process, each with its own world, props and the
// client's car; the client predicts its car from its inputs and corrects it
// on the host's states (as test_player_cars.cpp does), and its props are the
// host's (game::PropSync). With the pieces round its car in full, the client
// runs them with its car when its samples run again, as the host runs them
// with the host's simulation of its car.
#include "TestData.h"
#include "game/PlayerVehicle.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/net/PlayerCars.h"
#include "game/net/PropSync.h"
#include "net/PlayerCarState.h"
#include "net/PropState.h"
#include "phys/Level.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::game;
using bangers::BangerSet;
using bangers::PlacedProp;

namespace {

constexpr double kStepMs = 1000.0 / 60.0;

// A temporary game folder with a light prop's banger data (as test_prop_sync.cpp).
struct PushBangers {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("openmm2_proppush_") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    vfs::Vfs vfs;
    PushBangers() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "tune" / "banger");
        std::ofstream f(dir / "tune" / "banger" / "light.dgbangerdata");
        f << "type: a\ndgBangerData {\n  Size 0.5 2 0.5\n  CG 0 1 0\n  Mass 80\n  Elasticity 0.5\n"
             "  Friction 0.9\n  ImpulseLimit2 10000\n  NumParts 0\n  BirthRule {\n    InitialBlast 0\n  }\n"
             "  TexNumber 0\n  CollisionPrim 1\n}\n";
        f.close();
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~PushBangers() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

// One room with a floor whose objects are the set's props.
class PushLevel final : public phys::Level {
public:
    explicit PushLevel(const InstanceSource& source) : m_source(source) {}
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

// One machine: its world, its props (a pile of kRows rows of three, which
// the car's pushing knocks over one by one, and the pieces of one row the
// next) and the client's car.
constexpr int kRows = 6;

struct PushMachine {
    phys::World world;
    BangerSet set;
    PushLevel level{set};
    std::unique_ptr<SimVehicle> car;
    NetCarDriver driver;
    PushMachine(const vfs::Vfs& game, const bangers::BangerDataLibrary& lib) : set(lib) {
        world.setLevel(&level);
        set.setWorld(&world);
        set.recordKnocks(true);
        std::vector<PlacedProp> pile;
        for (int row = 0; row < kRows; ++row)
            for (int col = -1; col <= 1; ++col)
                pile.push_back({"light",
                                Mat34::translation({static_cast<float>(col) * 0.9f, 0.0f,
                                                    -8.0f - static_cast<float>(row) * 1.6f}),
                                1, PlacedProp::Source::Instance, true});
        set.add(pile);
        std::string error;
        car = SimVehicle::loadPlayer(game, "vpbug", &error, false);
        EXPECT_TRUE(car) << error;
        car->sim().options.player = true;
        car->sim().setPolygonalBound(true);
        car->sim().ownRandom = true;
        car->sim().randomState = 1;
        car->addTo(world);
        driver.attach(*car);
        car->setResetPos(Mat34::translation({0.0f, 0.6f, 0.0f}));
        car->reset();
    }
    ~PushMachine() { set.setWorld(nullptr); }
};

// A body round the car as a sample met it: the samples run again meet it
// there (as the race screen's bodyPoses and placeBodies).
struct BodyPose {
    phys::Body* body = nullptr;
    Mat34 ics, bound;
    Vec3 velocity, spin, kinematicVelocity, kinematicSpin;
};

std::vector<BodyPose> bodyPoses(PushMachine& m) {
    std::vector<phys::Body*> near;
    m.world.bodiesNear(m.car->sim().body.ics.matrix.m3, 40.0f, near);
    std::vector<BodyPose> out;
    for (phys::Body* b : near)
        if (b != &m.car->sim().body)
            out.push_back({b, b->ics.matrix, b->boundMatrix, b->ics.linearVelocity, b->ics.angularVelocity,
                           b->kinematicVelocity, b->kinematicSpin});
    return out;
}

// The bodies not run again where `poses` has them; the others out of the
// way (1 km below).
void placeBodies(PushMachine& m, const std::vector<BodyPose>& poses, const std::vector<BodyPose>& around,
                 const std::vector<const phys::Body*>& run) {
    for (const BodyPose& p : around) {
        if (std::ranges::find(run, p.body) != run.end() || !m.world.contains(p.body))
            continue;
        const auto it = std::ranges::find(poses, p.body, &BodyPose::body);
        const BodyPose& q = it != poses.end() ? *it : p;
        phys::Body& b = *p.body;
        b.ics.matrix = q.ics;
        b.boundMatrix = q.bound;
        b.ics.linearVelocity = q.velocity;
        b.ics.angularVelocity = q.spin;
        b.kinematicVelocity = q.kinematicVelocity;
        b.kinematicSpin = q.kinematicSpin;
        if (it == poses.end()) {
            b.ics.matrix.m3.y -= 1000.0f;
            b.boundMatrix.m3.y -= 1000.0f;
            b.ics.linearVelocity = b.ics.angularVelocity = b.kinematicVelocity = b.kinematicSpin = {};
        }
    }
}

template <class M>
struct InFlight {
    std::uint32_t due = 0;
    M msg;
};

// The client's input: gently into the pile, then on through it, pushing.
net::CarInputFrame pushInput(std::uint32_t seq) {
    phys::PedalInput p;
    p.accelerator = seq < 60 ? 0.6f : 0.35f;
    p.steering = seq > 240 && seq < 300 ? 0.2f : 0.0f;
    net::CarInputFrame f = inputFrame(p);
    f.flags = net::kInputAutomatic;
    return f;
}

struct PushOutcome {
    int corrections = 0;
    float largest = 0.0f;
    std::size_t knocked = 0;
    std::uint64_t fullPieces = 0;
};

// The client drives into the pile for `samples` samples; messages take
// `latency` samples each way; the host sends its states every sample (60 a
// second, as the race does), with the pieces round the car in full when
// `full` (PropHost::buildFull: about 20 a second).
PushOutcome push(const vfs::Vfs& game, bool full, int samples, std::uint32_t latency) {
    PushBangers files;
    bangers::BangerDataLibrary lib(files.vfs);
    PushMachine host(game, lib), client(game, lib);
    const phys::Instance* clientCar = &client.car->sim().body;
    client.set.setReplica([clientCar](const phys::Instance& other) { return &other == clientCar; });
    PropHost propHost;
    PropClient propClient(propCatalog(client.set));
    CarPrediction prediction;
    HostInputQueue queue;
    bool placed = false;
    struct Down {
        std::uint32_t ack = 0;
        net::OwnCarState own;
        std::vector<net::PropFullMsg> pieces;
        std::uint32_t time = 0;
    };
    struct Props {
        std::optional<net::PropStateMsg> state;
        std::optional<net::PropKnocksEvent> knocks;
    };
    std::deque<InFlight<net::PlayerInputMsg>> up;
    std::deque<InFlight<Down>> down;
    std::deque<InFlight<Props>> props;
    PushOutcome run;
    std::deque<std::pair<std::uint32_t, std::vector<BodyPose>>> history; // the client's, by sample
    const float dt = phys::kFixedSampleStep;
    const std::uint32_t catalog = propCatalog(host.set);
    for (std::uint32_t t = 1; t <= static_cast<std::uint32_t>(samples); ++t) {
        const double now = 10000.0 + static_cast<double>(t) * kStepMs;
        const auto time = static_cast<std::uint32_t>(now);
        // Client: the host's props, then its states, then its sample.
        while (!props.empty() && props.front().due <= t) {
            if (props.front().msg.state)
                propClient.receive(*props.front().msg.state, now);
            if (props.front().msg.knocks)
                propClient.receiveKnocks(*props.front().msg.knocks);
            props.pop_front();
        }
        propClient.update(client.set, now, nullptr, {}, client.car->sim().body.ics.matrix.m3);
        while (!down.empty() && down.front().due <= t) {
            Down& d = down.front().msg;
            // As NetProps::fullCompanions.
            std::vector<CarPrediction::Companion> companions;
            std::vector<const phys::Body*> replayed;
            phys::Body& car = client.car->sim().body;
            for (const auto& m : d.pieces)
                propClient.receiveFull(m);
            for (const auto& piece : propClient.fullPieces(client.set, d.time, now, &car)) {
                phys::Body* body = client.set.simulate(piece.instance);
                if (!body)
                    continue;
                replayed.push_back(body);
                CarPrediction::Companion c;
                c.body = body;
                c.rebase = [&client, &car, piece, body] {
                    client.set.setBodyState(piece.instance, piece.state, piece.pusher);
                    if (piece.pushedCar)
                        car.collider.lastMaxPusher = body->collider.key();
                };
                c.position = piece.state.matrix.m3;
                c.velocity = piece.state.linearVelocity;
                companions.push_back(std::move(c));
            }
            // The bodies the samples run again may meet: those a sample met
            // and those round the car now (as the race screen's).
            std::vector<BodyPose> around = bodyPoses(client);
            for (const auto& [seq, poses] : history)
                for (const BodyPose& p : poses)
                    if (seq > d.ack && std::ranges::find(around, p.body, &BodyPose::body) == around.end())
                        around.push_back(p);
            const auto beforeEach = [&](std::uint32_t seq) {
                for (const auto& [s, poses] : history)
                    if (s == seq)
                        placeBodies(client, poses, around, replayed);
            };
            const auto r = prediction.acknowledge(*client.car, client.driver, client.world, d.ack, d.own, {},
                                                  beforeEach, companions);
            placeBodies(client, around, around, replayed);
            propClient.forgetFull(d.time);
            if (r.corrected) {
                ++run.corrections;
                run.largest = std::max(run.largest, r.moved.mag());
            }
            down.pop_front();
        }
        if (prediction.nextSeq() == 1)
            prediction.command(*client.car, {0, net::CarCommandKind::ResetTo, client.car->sim().resetPos(),
                                             client.car->sim().resetRotation});
        history.emplace_back(prediction.nextSeq(), bodyPoses(client));
        while (history.size() > 240)
            history.pop_front();
        prediction.beginSample(*client.car, client.driver, pushInput(prediction.nextSeq()));
        client.world.step(dt);
        client.set.update(dt);
        propClient.predicted(client.set.takeKnocks(), now, 1,
                             [clientCar](const phys::Instance* by) { return by == clientCar ? 1 : -1; });
        prediction.endSample(*client.car, client.driver);
        if (const auto m = prediction.message())
            up.push_back({t + latency, *m});
        // Host: the client's inputs, its sample, its props' messages and the
        // client's state.
        while (!up.empty() && up.front().due <= t) {
            queue.receive(up.front().msg);
            up.pop_front();
        }
        if (!placed && queue.ready()) {
            if (const auto c = queue.placement()) {
                NetCarDriver::command(*host.car, *c);
                placed = true;
            }
        }
        if (placed) {
            if (auto next = queue.next()) {
                for (const auto& c : next->commands)
                    NetCarDriver::command(*host.car, c);
                host.driver.apply(*host.car, next->frame);
            }
        }
        host.world.step(dt);
        host.set.update(dt);
        propHost.knocked(host.set.takeKnocks(), time);
        for (auto& e : propHost.takeKnockEvents()) {
            Props p;
            net::PropKnocksEvent k;
            EXPECT_TRUE(net::decodePayload(e, k));
            p.knocks = k;
            props.push_back({t + latency, std::move(p)});
        }
        const Vec3 at = host.car->sim().body.ics.matrix.m3;
        const PropHost::Viewer viewer[] = {{1, at}};
        const auto nowMs = static_cast<std::uint64_t>(now);
        for (auto& [id, msg] : propHost.build(host.set, time, nowMs, catalog, viewer)) {
            Props p;
            p.state = std::move(msg);
            props.push_back({t + latency, std::move(p)});
        }
        if (placed && queue.lastApplied() != 0) {
            Down d;
            d.ack = queue.lastApplied();
            d.own = ownCarState(*host.car, 0);
            d.time = time;
            if (full) {
                d.pieces = propHost.buildFull(host.set, 1, time, at, &host.car->sim().body);
                for (const auto& m : d.pieces)
                    run.fullPieces += m.pieces.size();
            }
            down.push_back({t + latency, std::move(d)});
        }
    }
    for (std::size_t i = 0; i < placedProps(host.set); ++i)
        run.knocked += host.set.standing(i) ? 0 : 1;
    return run;
}

} // namespace

// A client's car driving on through a pile of props, pushing their pieces
// into the rest: with the host's pieces round it in full, run with it when
// its samples run again, it is (all but) never corrected; without, nearly
// every state is (it meets the host's pieces where they are shown, a
// playout delay in the past, or as its own simulation has them).
TEST(PropPush, ACarPushingPiecesIsPredictedWithTheirFullStates) {
    MM2_REQUIRE_GAME_DATA();
    const PushOutcome with = push(*test::gameData(), true, 8 * 60, 6);
    const PushOutcome without = push(*test::gameData(), false, 8 * 60, 6);
    std::printf("[ measure  ] pushing a pile: with the pieces in full %d corrections (largest %.3f m, %zu "
                "props knocked, %llu pieces sent); without %d (largest %.3f m, %zu knocked)\n",
                with.corrections, static_cast<double>(with.largest), with.knocked,
                static_cast<unsigned long long>(with.fullPieces), without.corrections,
                static_cast<double>(without.largest), without.knocked);
    EXPECT_GT(with.knocked, 12u);
    EXPECT_GT(with.fullPieces, 0u);
    EXPECT_LT(with.corrections * 20, without.corrections);
}
