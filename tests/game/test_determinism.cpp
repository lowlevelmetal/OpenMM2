// The simulation gives the same bits on every platform and compiler
// (docs/physics.md, "The same results on every platform"): a fixed scenario
// with no game data (a car on fixed inputs through props, over a jump and
// round and round a block of ambient traffic it knocks loose, for a minute)
// is hashed every two seconds of simulation and compared with hashes
// committed from a Linux GCC build. CI runs it on GCC, Clang, MinGW (under
// Wine) and MSVC; a host and a client built by two of those then simulate a
// network game's samples alike.
//
// When the simulation changes on purpose the hashes change with it: run
// test_game --gtest_filter='Determinism.*' and paste the table it prints.
// The new values must be the same on every platform (the CI jobs).
#include "ai/PlayerCar.h"
#include "ai/RoadNetwork.h"
#include "ai/Traffic.h"
#include "ai/TrafficLights.h"
#include "city/AiMap.h"
#include "core/File.h"
#include "game/CityLevel.h"
#include "game/TrafficBodies.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "phys/Constants.h"
#include "phys/Level.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "vfs/FileSystem.h"
#include "vfs/Vfs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cfenv>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace mm2;

namespace {

constexpr float kDt = phys::kFixedSampleStep;
constexpr int kSteps = 3600;     // 60 s
constexpr int kCheckEvery = 120; // 2 s

// --- Hashing -----------------------------------------------------------------------------------

// FNV-1a over the bits of the state, field by field (no padding).
struct Hash {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void add(float v) {
        const std::uint32_t u = std::bit_cast<std::uint32_t>(v);
        bytes(&u, sizeof u);
    }
    void add(std::int64_t v) { bytes(&v, sizeof v); }
    void add(const Vec3& v) {
        add(v.x);
        add(v.y);
        add(v.z);
    }
    void add(const Mat34& m) {
        add(m.m0);
        add(m.m1);
        add(m.m2);
        add(m.m3);
    }
};

// --- Game files in memory ----------------------------------------------------------------------

class MemoryFs final : public vfs::FileSystem {
public:
    void put(std::string path, std::string_view text) { m_files[std::move(path)] = std::string(text); }
    std::string describe() const override { return "memory"; }
    std::shared_ptr<RandomAccessFile> open(std::string_view path) const override {
        const auto it = m_files.find(std::string(path));
        if (it == m_files.end())
            return nullptr;
        std::vector<std::byte> data(it->second.size());
        std::memcpy(data.data(), it->second.data(), data.size());
        return std::make_shared<MemoryFile>(std::move(data));
    }
    bool exists(std::string_view path) const override { return m_files.contains(std::string(path)); }
    void forEachFile(const std::function<void(const vfs::EntryInfo&)>& fn) const override {
        for (const auto& [path, text] : m_files)
            fn({path, text.size(), false});
    }

private:
    std::map<std::string, std::string> m_files;
};

// Made-up props (no game data): a light one that breaks loose at the
// slightest knock, a heavy one, and one that splits into two parts.
std::string bangerFile(const char* size, const char* cg, const char* mass, const char* limit2, int parts) {
    return std::format("type: a\ndgBangerData {{\n  Size {}\n  CG {}\n  Mass {}\n  Elasticity 0.4\n"
                       "  Friction 0.8\n  ImpulseLimit2 {}\n  NumParts {}\n  BirthRule {{\n"
                       "    InitialBlast 0\n  }}\n  TexNumber 0\n  CollisionPrim 1\n}}\n",
                       size, cg, mass, limit2, parts);
}

std::shared_ptr<MemoryFs> bangerFiles() {
    auto fs = std::make_shared<MemoryFs>();
    fs->put("tune/banger/cone.dgbangerdata", bangerFile("0.4 0.8 0.4", "0 0.4 0", "15", "100", 0));
    fs->put("tune/banger/crate.dgbangerdata", bangerFile("1.2 1.2 1.2", "0 0.6 0", "300", "40000", 0));
    fs->put("tune/banger/sign.dgbangerdata", bangerFile("0.3 2.5 0.3", "0 1.25 0", "60", "2500", 2));
    fs->put("tune/banger/sign_break01.dgbangerdata", bangerFile("0.3 1.2 0.3", "0 0.6 0", "30", "1e6", 0));
    fs->put("tune/banger/sign_break02.dgbangerdata", bangerFile("0.3 1.2 0.3", "0 1.85 0", "30", "1e6", 0));
    return fs;
}

// --- The city ----------------------------------------------------------------------------------

// A straight two-way road from a to b, one lane each way 4 m apart, laid out
// as the retail files are (see tests/ai/test_traffic.cpp).
city::AiPath road(int id, Vec3 a, Vec3 b, std::uint32_t nodeA, std::uint32_t nodeB) {
    city::AiPath p;
    p.id = static_cast<std::uint16_t>(id);
    p.flags = 0x8;
    p.halfWidth = 7.0f;
    const Vec3 d = (b - a).normalized();
    const Vec3 left{d.z, 0.0f, -d.x};
    constexpr int kSections = 4;
    for (int i = 0; i < kSections; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSections - 1);
        p.center.push_back(lerp(a, b, t));
        p.xAxis.push_back(left);
        p.yAxis.push_back(Vec3::yAxis());
        p.zAxis.push_back(-d);
        p.wAxis.push_back(d);
        if (i > 0)
            p.centerLengths.push_back(a.dist(b) * t);
    }
    auto side = [&](float sign, bool reversed) {
        city::AiRoadSide s;
        s.numLanes = 1;
        s.numSidewalks = 1;
        std::vector<Vec3> lane, walk, curb, edge;
        for (int i = 0; i < kSections; ++i) {
            const Vec3& c = p.center[static_cast<std::size_t>(reversed ? kSections - 1 - i : i)];
            lane.push_back(c + left * (sign * 2.0f));
        }
        for (int i = 0; i < kSections; ++i) {
            const Vec3& c = p.center[static_cast<std::size_t>(i)];
            walk.push_back(c + left * (sign * 5.5f));
            curb.push_back(c + left * (sign * 4.0f));
            edge.push_back(c + left * (sign * 7.0f));
        }
        s.polylines = {lane, walk, curb, edge};
        return s;
    };
    p.left = side(1.0f, true);
    p.right = side(-1.0f, false);
    p.ends[0].intersection = nodeB;
    p.ends[1].intersection = nodeA;
    p.ends[0].vehicleRule = 3;
    p.ends[1].vehicleRule = 3;
    return p;
}

// A block 200 m a side; road k runs from corner k to corner k + 1, and
// corner 1 has traffic lights.
city::AiMap block() {
    city::AiMap map;
    const Vec3 corner[4] = {{0, 0, 0}, {0, 0, -200}, {200, 0, -200}, {200, 0, 0}};
    for (int k = 0; k < 4; ++k)
        map.paths.push_back(road(k, corner[k], corner[(k + 1) % 4], static_cast<std::uint32_t>(k),
                                 static_cast<std::uint32_t>((k + 1) % 4)));
    for (int k = 0; k < 4; ++k)
        map.intersections.push_back(
            {static_cast<std::uint16_t>(k), 0, corner[k],
             {static_cast<std::uint32_t>((k + 3) % 4), static_cast<std::uint32_t>(k)}});
    map.paths[0].ends[0].vehicleRule = 1;
    map.paths[1].ends[1].vehicleRule = 1;
    for (auto& node : map.intersections)
        for (std::size_t k = 0; k < node.paths.size(); ++k)
            for (auto& e : map.paths[node.paths[k]].ends)
                if (e.intersection == node.id)
                    e.roadIndex = static_cast<std::uint16_t>(k);
    return map;
}

// Made-up traffic cars, with vehWheelCheap's own spring and tyre values.
ai::VehicleData sedan() {
    ai::VehicleData d;
    d.model = "sedan";
    d.mass = 1400.0f;
    d.size = {1.9f, 1.5f, 4.6f};
    d.maxAng = {3.0f, 3.0f, 3.0f};
    d.cg = {0.0f, 0.7f, 0.0f};
    d.spring = 50000.0f;
    d.damping = 5000.0f;
    d.rubberSpring = 40000.0f;
    d.rubberDamp = 2000.0f;
    d.limit = 0.07f;
    d.wheelRadius = 0.33f;
    d.wheels = {Vec3{-0.8f, 0.33f, -1.4f}, Vec3{0.8f, 0.33f, -1.4f}, Vec3{-0.8f, 0.33f, 1.4f},
                Vec3{0.8f, 0.33f, 1.4f}, Vec3{}, Vec3{}};
    d.wheelCount = 4;
    return d;
}

// The level as the collision manager sees it: one room with the ground, a
// wall across the far end of road 0, a ramp beside it, and the instances of
// the props and the traffic.
class ScenarioLevel final : public phys::Level {
public:
    void addSource(const game::InstanceSource* s) { m_sources.push_back(s); }
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override {
        out.clear();
        const Vec3 ground[4] = {{-2000, 0, -2000}, {-2000, 0, 2000}, {2000, 0, 2000}, {2000, 0, -2000}};
        out.addPolygon(ground, 4, {0, 1, 0}, 0);
        const Vec3 wall[4] = {{-30, 0, -230}, {30, 0, -230}, {30, 6, -230}, {-30, 6, -230}};
        out.addPolygon(wall, 4, {0, 0, 1}, 0);
        // A jump on road 0: rising 1.2 m over 15 m.
        const Vec3 ramp[4] = {{-3, 0, -180}, {3, 0, -180}, {3, 1.2f, -195}, {-3, 1.2f, -195}};
        const Vec3 n = (ramp[2] - ramp[1]).cross(ramp[0] - ramp[1]).normalized();
        out.addPolygon(ramp, 4, n, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        for (const game::InstanceSource* s : m_sources)
            s->instancesIn(room, out);
    }

private:
    std::vector<const game::InstanceSource*> m_sources;
};

// TrafficBodies' view of the AI: the cars and the hand-over (as ai::World's).
class TrafficSource final : public game::TrafficBodies::Source {
public:
    explicit TrafficSource(ai::Traffic& traffic) : m_traffic(traffic) {}
    const std::vector<ai::AmbientCar>& cars() const override { return m_traffic.cars(); }
    void impact(int carId) override { m_traffic.impact(carId, {}); }
    void detach(int carId, const Mat34& pose, bool upright) override {
        m_traffic.detach(carId, pose, upright);
    }
    void setPhysicalTransform(int carId, const Mat34& transform) override {
        m_traffic.setPhysicalTransform(carId, transform);
    }

private:
    ai::Traffic& m_traffic;
};

// The driver's inputs for sample `i`: off the line up the lane of traffic,
// through the props, over the jump towards the wall, braking into reverse,
// a handbrake turn, then circling and weaving through both lanes.
phys::PedalInput inputs(int i, float& steerTarget) {
    phys::PedalInput in;
    const float t = static_cast<float>(i) * kDt;
    if (t < 1.0f)
        return in;
    in.accelerator = 1.0f;
    if (t > 12.0f && t < 15.0f) {
        in.accelerator = 0.0f;
        in.brake = 1.0f; // and on into reverse
    } else if (t > 15.0f && t < 16.5f) {
        in.handbrake = 1.0f;
        steerTarget = 1.0f;
    } else if (t > 16.5f && t < 38.0f) {
        steerTarget = 0.25f;
    } else if (t > 38.0f) {
        steerTarget = -0.3f;
    }
    return in;
}

// A made-up sports car on OpenMM2's default tune and body.
phys::CarSimParams sportsCar() {
    phys::CarSimParams p;
    p.mass = 1400.0f;
    p.inertiaBox = {1.9f, 1.3f, 4.4f};
    p.engine.maxHorsePower = 450.0f;
    p.trans.high = 120.0f;
    return p;
}

struct Checkpoint {
    int step = 0;
    std::uint64_t car = 0;
    std::uint64_t traffic = 0;
    std::uint64_t props = 0;
};

struct ScenarioRun {
    std::vector<Checkpoint> checkpoints;
    std::vector<std::string> notes; // per checkpoint: where the car is, what it hit
    std::set<int> knockedCars;      // traffic cars that took a body
    int movedProps = 0;             // props that left their place
};

ScenarioRun runScenario() {
    ScenarioRun run;
    // Props (from memory) and the physics world.
    vfs::Vfs files;
    files.mount(bangerFiles());
    game::bangers::BangerDataLibrary bangerData(files);
    ScenarioLevel level;
    phys::World world;
    world.setLevel(&level);
    world.seedRandom(1);

    // The traffic on its block, and its cars in the physics world.
    const city::AiMap map = block();
    const ai::RoadNetwork net = ai::RoadNetwork::build(map, {});
    ai::TrafficLights lights;
    lights.build(net);
    ai::TrafficSettings settings;
    settings.density = 1.0f;
    ai::Traffic traffic(net, lights, {sedan()}, settings, 7);
    traffic.populateAll();
    TrafficSource source(traffic);
    game::TrafficBodies trafficBodies(source, world);
    level.addSource(&trafficBodies);

    // Props along road 0 ahead of the car.
    game::bangers::BangerSet props(bangerData);
    std::vector<game::bangers::PlacedProp> placed;
    auto place = [&](const char* model, float x, float z, float turn) {
        Mat34 m = Mat34::rotationY(turn);
        m.m3 = {x, 0.0f, z};
        placed.push_back({model, m, 1, game::bangers::PlacedProp::Source::Instance, true});
    };
    for (int k = 0; k < 10; ++k)
        place("cone", 2.0f + (k % 2 == 0 ? -0.8f : 0.8f), -30.0f - 6.0f * static_cast<float>(k),
              0.3f * static_cast<float>(k));
    place("crate", 2.3f, -95.0f, 0.4f);
    place("sign", 0.8f, -110.0f, 0.0f);
    place("sign", 3.2f, -140.0f, 1.0f);
    place("crate", 1.5f, -170.0f, -0.2f);
    props.add(placed);
    level.addSource(&props);
    props.setWorld(&world);

    phys::CarSim car;
    car.init(sportsCar(), phys::VehicleGeometry::placeholder());
    car.setPolygonalBound(true);
    Mat34 start = Mat34::identity();
    start.m3 = {2.0f, 0.5f, -6.0f};
    car.reset(start);
    world.add(&car.body);
    phys::ArcadeControls controls;
    phys::SteeringFilter steering;

    for (int i = 0; i < kSteps; ++i) {
        // The AI at its own 30 Hz, before the physics (as RaceScreen).
        if (i % 2 == 0) {
            ai::PlayerCar player;
            player.transform = car.body.ics.matrix;
            player.velocity = car.body.ics.linearVelocity;
            lights.update(ai::kAiStepSeconds);
            traffic.step(ai::kAiStepSeconds, player, 0);
        }
        float target = 0.0f;
        phys::PedalInput in = inputs(i, target);
        in.steering = steering.filter(target, kDt);
        steering.setSpeed(car.speed());
        controls.apply(car, in);
        trafficBodies.beforeStep();
        world.step(kDt);
        props.update(kDt);
        trafficBodies.afterStep();
        for (const ai::AmbientCar& a : traffic.cars())
            if (trafficBodies.transformOf(a.id))
                run.knockedCars.insert(a.id);

        if ((i + 1) % kCheckEvery != 0)
            continue;
        Checkpoint c;
        c.step = i + 1;
        Hash hc;
        hc.add(car.body.ics.matrix);
        hc.add(car.body.ics.linearVelocity);
        hc.add(car.body.ics.angularVelocity);
        for (const phys::Wheel& w : car.wheels) {
            hc.add(w.rotationSpeed);
            hc.add(w.suspension);
        }
        hc.add(car.engine.rpm);
        hc.add(static_cast<std::int64_t>(car.trans.currentGear));
        hc.add(car.damage.currentDamage);
        hc.add(static_cast<std::int64_t>(*world.randomSeed()));
        c.car = hc.h;
        Hash ht;
        for (const ai::AmbientCar& a : traffic.cars()) {
            ht.add(static_cast<std::int64_t>(a.id));
            ht.add(a.transform);
            ht.add(a.speed);
            if (const Mat34* m = trafficBodies.transformOf(a.id))
                ht.add(*m);
        }
        c.traffic = ht.h;
        Hash hp;
        int moved = 0;
        for (const auto& p : props.instances()) {
            hp.add(p.matrix);
            hp.add(static_cast<std::int64_t>(p.state));
            moved += p.state != game::bangers::BangerSet::State::Unhit;
        }
        run.movedProps = std::max(run.movedProps, moved);
        c.props = hp.h;
        run.checkpoints.push_back(c);
        const Vec3& p = car.body.ics.matrix.m3;
        run.notes.push_back(std::format("{:2.0f} s: car ({:.2f} {:.2f} {:.2f}) {:.1f} m/s damage {:.0f}, "
                                        "x {:a}; {} cars knocked, {} props moved",
                                        static_cast<float>(c.step) * kDt, p.x, p.y, p.z, car.speed(),
                                        car.damage.currentDamage, p.x, run.knockedCars.size(), moved));
    }
    return run;
}

// From a Linux GCC build; every platform must match them.
constexpr Checkpoint kExpected[] = {
    {120, 0x3a3c70fe586627f4, 0x232b43b2bd3790f5, 0xb09b3c26cd8491f7},
    {240, 0xf44a3b1523419836, 0x07c07745035920ff, 0x1f857ed1cc92639c},
    {360, 0x6636072d75e5e70b, 0x9c83a9eb6e1d0701, 0x4625eefff3380c42},
    {480, 0xcc99546d20f06ace, 0x014e85b48382d19b, 0xd1a0dda3b99b98c2},
    {600, 0xc93d8ea3e4d97515, 0xa45510819c5caa3f, 0x9b20f4fcb7081d7b},
    {720, 0xcbff89324d9fc812, 0x3d9829c47749bf73, 0xcda29e43be99bfc7},
    {840, 0x8857e8bd0f88da1a, 0x1b987cda49fc2236, 0xfbfbd6f4d19d434d},
    {960, 0x9bce1e35dd3b7d09, 0xcaa7d5466fc9d822, 0xfbfbd6f4d19d434d},
    {1080, 0x0f34a38e28017b62, 0x95cd177ee51c5cdf, 0xfbfbd6f4d19d434d},
    {1200, 0xf1cf1f60a2b3cdb7, 0x29d554768d7bae0a, 0xfbfbd6f4d19d434d},
    {1320, 0x9baa47913191c83d, 0x0f2c14141a6658b6, 0xfbfbd6f4d19d434d},
    {1440, 0x62032f6974458ffe, 0x87709e323c4d7f0a, 0xfbfbd6f4d19d434d},
    {1560, 0x8ac89bb2fdc0dca4, 0x1f6a24acb4aa99cb, 0xfbfbd6f4d19d434d},
    {1680, 0xcbabc0f348538d48, 0xaf5f5de0861f667f, 0xfbfbd6f4d19d434d},
    {1800, 0x73e4a52494f9f6e2, 0x5f46e4c1ecfb7e2d, 0xfbfbd6f4d19d434d},
    {1920, 0x13867b1c2b431420, 0xe2f3b1b45a0015f1, 0xfbfbd6f4d19d434d},
    {2040, 0xfcec09922ecc3586, 0x51b5aea0fb69eb7a, 0xfbfbd6f4d19d434d},
    {2160, 0x92995741f1f6e8b2, 0x754b679d69746280, 0xfbfbd6f4d19d434d},
    {2280, 0x9272324f6533ab08, 0xf6e2a9f52d9ac74f, 0xfbfbd6f4d19d434d},
    {2400, 0xf4a8159c6c341e8a, 0x771d45cc7d7d83e6, 0xfbfbd6f4d19d434d},
    {2520, 0xc85c1127eb6826c6, 0x5211769ee6ca9265, 0xfbfbd6f4d19d434d},
    {2640, 0x5b41151bd126696a, 0x3b69c0ce31e6426c, 0xfbfbd6f4d19d434d},
    {2760, 0x4084751e2380d5a7, 0x06921e31cb806c4b, 0xfbfbd6f4d19d434d},
    {2880, 0xa453ea47f6c395c0, 0x23cc0dbda0264c29, 0xfbfbd6f4d19d434d},
    {3000, 0x89728248115b48ff, 0x7528abb5441d5154, 0xfbfbd6f4d19d434d},
    {3120, 0xf5ea3c8def065bb4, 0xad25ecafb12c28e0, 0xfbfbd6f4d19d434d},
    {3240, 0x3c6a79fc019cdc90, 0x241b6275021a6397, 0xfbfbd6f4d19d434d},
    {3360, 0xa3ae9659ea926806, 0x1b566fd8551815e8, 0xfbfbd6f4d19d434d},
    {3480, 0x1efab959e2778136, 0x87dbb2fe9ab88ca4, 0xfbfbd6f4d19d434d},
    {3600, 0xfa6b5022efc7f63a, 0x0a248681704e70f9, 0xfbfbd6f4d19d434d},
};

// The hashes as source, then what happened by each checkpoint (to compare
// with another platform's log).
std::string table(const ScenarioRun& run) {
    std::string s = "constexpr Checkpoint kExpected[] = {\n";
    for (const Checkpoint& c : run.checkpoints)
        s += std::format("    {{{}, {:#018x}, {:#018x}, {:#018x}}},\n", c.step, c.car, c.traffic, c.props);
    s += "};\n";
    for (const std::string& note : run.notes)
        s += note + "\n";
    return s;
}

} // namespace

// Floats and doubles are evaluated in their own type (SSE2, AArch64; an x87
// build would round its intermediates differently), rounded to nearest, with
// subnormals kept (nothing set flush-to-zero).
TEST(Determinism, FloatEnvironmentIsIeee) {
#ifdef FLT_EVAL_METHOD
    EXPECT_EQ(FLT_EVAL_METHOD, 0);
#endif
    EXPECT_EQ(std::fegetround(), FE_TONEAREST);
    volatile float a = 1.0f, b = 0x1p-24f;
    EXPECT_EQ(a + b - a, 0.0f); // rounded to float before the subtraction
    volatile float tiny = 0x1p-140f;
    EXPECT_EQ(tiny * 0.5f, 0x1p-141f);
}

TEST(Determinism, ScenarioHashesMatchEveryPlatform) {
    const ScenarioRun run = runScenario();
    ASSERT_EQ(run.checkpoints.size(), static_cast<std::size_t>(kSteps / kCheckEvery));
    std::printf("determinism: at the end %s\n", run.notes.back().c_str());
    // The scenario does what it is for.
    EXPECT_GT(run.knockedCars.size(), 2u);
    EXPECT_GT(run.movedProps, 5);
    bool same = std::size(kExpected) == run.checkpoints.size();
    for (std::size_t i = 0; same && i < run.checkpoints.size(); ++i) {
        const Checkpoint& got = run.checkpoints[i];
        const Checkpoint& want = kExpected[i];
        if (got.step == want.step && got.car == want.car && got.traffic == want.traffic &&
            got.props == want.props)
            continue;
        same = false;
        ADD_FAILURE() << std::format("first difference at step {} ({:.0f} s): car {}, traffic {}, props {}",
                                     got.step, static_cast<float>(got.step) * kDt,
                                     got.car == want.car ? "same" : "differs",
                                     got.traffic == want.traffic ? "same" : "differs",
                                     got.props == want.props ? "same" : "differs");
    }
    if (!same)
        ADD_FAILURE() << "this platform's hashes:\n" << table(run);
}
