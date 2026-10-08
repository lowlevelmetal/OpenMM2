// AI opponents and police driving physics cars through the retail cities.
//
// Environment:
//   OPENMM2_AI_TRAILS=<dir>     top-down plots of the runs (lanes grey, curbs
//                               dark grey, driving lines blue, trails coloured)
//   OPENMM2_AI_ZOOM=x,y,z,r     also a close-up with the walls around a point
//   OPENMM2_AI_SWEEP=1          run EveryRaceSweep (all circuits and checkpoint
//                               races of both cities, both difficulties)
//   OPENMM2_AI_ONLY="sf race8 a"  limit the sweep to one race
//   OPENMM2_AI_DEBUG=1|2|3      print courses / trace one car
//                               (OPENMM2_AI_TRACE=<car>) / stuck events
// e.g. OPENMM2_AI_TRAILS=local/out/ai2 test_game --gtest_filter='OpponentRace.*:PoliceChase.*'
#include "TestData.h"
#include "ai/Opponent.h"
#include "ai/Police.h"
#include "ai/World.h"
#include "asset/Image.h"
#include "city/CityData.h"
#include "core/File.h"
#include "game/CityLevel.h"
#include "game/PlayerVehicle.h"
#include "game/TrafficBodies.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/session/RaceSetup.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace mm2;

namespace {

constexpr float kDt = 1.0f / 30.0f;

int debugLevel() {
    const char* d = std::getenv("OPENMM2_AI_DEBUG");
    return d ? std::atoi(d) : 0;
}

// The city as RaceScreen sets it up: the level the collision manager sees
// (rooms, collision polygons, collidable objects), the props every car can
// knock over, ambient traffic that turns solid when touched.
struct CityWorld {
    std::optional<city::CityData> city;
    std::unique_ptr<game::bangers::BangerDataLibrary> bangerData;
    std::unique_ptr<game::CityLevel> level;
    std::unique_ptr<phys::World> world;
    std::unique_ptr<game::bangers::BangerSet> bangers;
    std::unique_ptr<ai::World> ai;
    std::unique_ptr<game::TrafficBodies> traffic;

    static std::unique_ptr<CityWorld> load(const vfs::Vfs& vfs, const char* name, float trafficDensity) {
        auto w = std::make_unique<CityWorld>();
        w->city = city::loadCity(vfs, name);
        if (!w->city)
            return nullptr;
        w->bangerData = std::make_unique<game::bangers::BangerDataLibrary>(vfs);
        w->level = std::make_unique<game::CityLevel>(
            *w->city, vfs, [&](std::string_view n) { return w->bangerData->has(n); });
        w->world = std::make_unique<phys::World>(w->level->takeMaterials());
        w->world->setStatic(w->level->takeProbeSoup());
        w->world->setLevel(w->level.get());
        w->bangers = std::make_unique<game::bangers::BangerSet>(*w->bangerData);
        w->bangers->add(game::bangers::placeCityProps(*w->city, vfs, *w->bangerData));
        w->level->addSource(w->bangers.get());
        w->bangers->setWorld(w->world.get());
        ai::Settings settings;
        settings.trafficDensity = trafficDensity;
        settings.pedestrianDensity = 0.0f;
        w->ai = ai::World::create(*w->city, vfs, settings);
        if (!w->ai)
            return nullptr;
        if (trafficDensity > 0.0f) {
            w->traffic = std::make_unique<game::TrafficBodies>(*w->ai, *w->world);
            w->level->addSource(w->traffic.get());
        }
        return w;
    }

    // One frame after the AI has set the cars' inputs.
    void step(std::span<phys::Body* const> /*vehicles*/, const Vec3& focus) {
        if (traffic)
            traffic->beforeStep();
        world->advanceFixed(kDt);
        bangers->update(kDt);
        if (traffic) {
            traffic->afterStep();
            ai->update(kDt, focus, {});
        }
    }
};

// MM2 builds every AI car with its base tune (aiVehiclePhysics::Init ->
// vehCar::Init(<car>)); the retail *_opp / *_cop tunes are MM1 leftovers.
std::unique_ptr<game::SimVehicle> spawnCar(const vfs::Vfs& vfs, phys::World& world, const std::string& name,
                                           Mat34 spawn, std::string_view tune = {}) {
    std::string error;
    auto car = game::SimVehicle::load(vfs, name, &error, tune);
    if (!car)
        return nullptr;
    car->addTo(world);
    phys::RayHit hit;
    if (world.probe(spawn.m3 + Vec3{0, 5, 0}, spawn.m3 - Vec3{0, 30, 0}, hit))
        spawn.m3 = hit.position;
    car->reset(spawn);
    return car;
}

ai::TrackedCar track(const game::SimVehicle& v, int id, bool player = false) {
    return ai::trackedCar(v.sim(), id, player);
}

void addAmbient(std::vector<ai::TrackedCar>& out, const ai::World& ai) {
    for (const ai::AmbientCar& c : ai.cars())
        out.push_back(ai::trackedAmbient(c, 10000 + c.id));
}

// Top-down plot, at most 2 px per metre.
struct Plot {
    float x0 = 0, z0 = 0, scale = 2.0f;
    asset::Image::Level img;

    Plot(const Aabb& bounds, float pad = 40.0f, float maxScale = 2.0f) {
        x0 = bounds.min.x - pad;
        z0 = bounds.min.z - pad;
        const float w = bounds.max.x - bounds.min.x + 2 * pad, h = bounds.max.z - bounds.min.z + 2 * pad;
        scale = std::min(maxScale, 2400.0f / std::max(w, h));
        img.width = static_cast<std::uint32_t>(std::max(64.0f, w * scale));
        img.height = static_cast<std::uint32_t>(std::max(64.0f, h * scale));
        img.rgba.assign(std::size_t{img.width} * img.height * 4, 255);
    }
    void dot(float x, float z, std::uint32_t rgb, int r = 0) {
        const int px = static_cast<int>((x - x0) * scale), pz = static_cast<int>((z - z0) * scale);
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const int ix = px + dx, iz = pz + dz;
                if (ix < 0 || iz < 0 || ix >= static_cast<int>(img.width) || iz >= static_cast<int>(img.height))
                    continue;
                // Rows are stored bottom-up for encodePng: +Z ends up at the bottom.
                const std::size_t row = img.height - 1 - static_cast<std::size_t>(iz);
                std::uint8_t* p = &img.rgba[(row * img.width + static_cast<std::size_t>(ix)) * 4];
                p[0] = static_cast<std::uint8_t>(rgb >> 16);
                p[1] = static_cast<std::uint8_t>(rgb >> 8);
                p[2] = static_cast<std::uint8_t>(rgb);
                p[3] = 255;
            }
    }
    void line(const Vec3& a, const Vec3& b, std::uint32_t rgb, int r = 0) {
        const int n = std::max(1, static_cast<int>(Vec2{b.x - a.x, b.z - a.z}.mag() * scale));
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            dot(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, rgb, r);
        }
    }
    void roads(const ai::RoadNetwork& net) {
        for (const ai::Lane& lane : net.lanes())
            for (std::size_t i = 1; i < lane.line.points.size(); ++i)
                line(lane.line.points[i - 1], lane.line.points[i], 0xC8C8C8);
        if (const auto* map = net.source())
            for (const auto& p : map->paths)
                for (std::size_t k = 0; k < p.center.size(); ++k) {
                    float l, r;
                    ai::pathCurbs(p, k, l, r);
                    const Vec3 x = p.xAxis[k];
                    dot(p.center[k].x + x.x * l, p.center[k].z + x.z * l, 0x808080, 1);
                    dot(p.center[k].x - x.x * r, p.center[k].z - x.z * r, 0x808080, 1);
                }
    }
    void walls(const phys::World& world, const Aabb& box) {
        std::vector<std::uint32_t> ids;
        world.staticGeometry().query(box, ids);
        for (std::uint32_t i : ids) {
            const auto& poly = world.staticGeometry().polygon(i);
            const bool wall = std::abs(poly.normal.y) < 0.6f;
            for (int k = 0; k < poly.count; ++k)
                line(poly.v[static_cast<std::size_t>(k)], poly.v[static_cast<std::size_t>((k + 1) % poly.count)],
                     wall ? 0x202020 : 0xE8E8E8);
        }
    }
    void polyline(const std::vector<Vec3>& pts, std::uint32_t rgb, int r = 0) {
        for (std::size_t i = 1; i < pts.size(); ++i)
            line(pts[i - 1], pts[i], rgb, r);
    }
    void save(const std::string& name) const {
        const char* dir = std::getenv("OPENMM2_AI_TRAILS");
        if (!dir || !*dir)
            return;
        std::filesystem::create_directories(dir);
        file::writeAtomic(std::filesystem::path(dir) / name, asset::encodePng(img));
    }
};

constexpr std::uint32_t kColours[] = {0xE6194B, 0x3CB44B, 0xF58231, 0x911EB4, 0x46F0F0, 0xF032E6, 0x808000, 0x008080};

struct AiRacer {
    std::unique_ptr<game::SimVehicle> vehicle;
    std::unique_ptr<ai::Opponent> driver;
    int id = 0;
    std::vector<Vec3> trail;
    std::vector<float> lapTimes;
    int lapsSeen = 0;
    float lastLapStart = 0.0f;
    float finishTime = -1.0f;
    // Excursions more than 1 m beyond the sidewalk: those that stay at road
    // level, and those that drop below a raised road (see beyondCurb).
    float worstOffRoad = 0.0f; // metres beyond the sidewalk, at road level
    float offRoadSeconds = 0.0f;
    float offRoadFirstLap = 0.0f; // part of offRoadSeconds in excursions that began on the first lap
    float belowRoadSeconds = 0.0f;
    struct Excursion {
        float seconds = 0.0f, worst = 0.0f;
        bool onFirstLap = false, dropped = false;
    } excursion;
    bool offRoad = false;
    int seenResets = 0, seenBackups = 0;

    void endExcursion() {
        if (excursion.dropped) {
            belowRoadSeconds += excursion.seconds;
        } else {
            offRoadSeconds += excursion.seconds;
            if (excursion.onFirstLap)
                offRoadFirstLap += excursion.seconds;
            worstOffRoad = std::max(worstOffRoad, excursion.worst);
        }
        excursion = {};
    }
};

// Distance beyond the course's sidewalks (0 on the road or a sidewalk: MM2's
// racers take the sidewalk to get round an obstacle, CalcObstacleAvoidPoints
// accepting points there). `below` is set when the car is also more than
// 2.5 m under the road there: it has dropped off a raised road (London's
// are 5-7 m up without barriers), not driven off at road level.
float beyondCurb(const ai::Opponent& o, const Vec3& pos, bool* below = nullptr) {
    const ai::Course& c = o.course();
    float lateral = 0.0f;
    const float s = c.locate(pos, o.courseDistance(), 40.0f, &lateral);
    float left, right, leftEdge, rightEdge;
    c.edges(s, left, right, &leftEdge, &rightEdge);
    if (below)
        *below = pos.y < c.pointAt(s).y - 2.5f;
    return std::max(0.0f, lateral > 0.0f ? lateral - rightEdge : -lateral - leftEdge);
}

struct RaceRun {
    std::vector<AiRacer> racers;
    float time = 0.0f;
};

void printCourse(const AiRacer& r, const std::string& file) {
    const ai::Course& c = r.driver->course();
    std::printf("car %d %s: start s %.1f of %.1f, finish %.1f, intersections", r.id, file.c_str(), c.startDistance(),
                c.length(), c.finishDistance());
    for (int n : c.intersections())
        std::printf(" %d", n);
    std::printf("\n  turns:");
    for (const auto& t : c.turns())
        std::printf(" [s %.0f d %.2f w %.1f]", t.s, t.deflection, t.halfWidth);
    std::printf("\n");
}

void trace(const RaceRun& run) {
    const char* id = std::getenv("OPENMM2_AI_TRACE");
    const std::size_t i = std::min<std::size_t>(id ? std::strtoul(id, nullptr, 10) - 1 : 0, run.racers.size() - 1);
    const AiRacer& r = run.racers[i];
    const phys::CarSim& sim = r.vehicle->sim();
    const Vec3 p = sim.body.ics.matrix.m3;
    const Vec3 aim = r.driver->targetPoint();
    std::printf("t %5.1f pos (%.1f %.1f %.1f) prog %6.1f v %5.1f thr %.2f brk %.2f str %+.2f gear %d mode %d "
                "side %+.1f aim (%.1f %.1f) curb %.1f\n",
                run.time, p.x, p.y, p.z, r.driver->progress(), sim.speed(), sim.engine.throttle, sim.brakes,
                sim.steering, sim.trans.getCurrentGear(), static_cast<int>(r.driver->mode()), r.driver->side(), aim.x,
                aim.z, beyondCurb(*r.driver, sim.modelMatrix().m3));
}

void printStuck(AiRacer& r, float time, const Vec3& pos) {
    if (r.driver->resets() == r.seenResets && r.driver->backups() == r.seenBackups)
        return;
    float l, rr;
    r.driver->course().edges(r.driver->courseDistance(), l, rr);
    std::printf("  t %.1f car %d %s at (%.1f %.1f %.1f) s %.1f side %+.1f edges %.1f/%.1f\n", time, r.id,
                r.driver->resets() != r.seenResets ? "reset" : "backup", pos.x, pos.y, pos.z,
                r.driver->courseDistance(), r.driver->side(), l, rr);
    r.seenResets = r.driver->resets();
    r.seenBackups = r.driver->backups();
}

// Runs a race with every opponent of the setup (no player), as RaceScreen
// would: hold during a 1 s countdown, then AI, then the physics step.
RaceRun runRace(CityWorld& cw, const vfs::Vfs& vfs, const game::session::RaceSetup& setup, float maxSeconds) {
    RaceRun run;
    const ai::RoadNetwork& net = cw.ai->network();
    int id = 1;
    for (const auto& o : setup.opponents) {
        AiRacer r;
        r.vehicle = spawnCar(vfs, *cw.world, o.vehicle, o.spawn);
        if (!r.vehicle)
            continue;
        r.id = id++;
        std::string error;
        r.driver = ai::Opponent::create(net, r.vehicle->sim(), o.path, o.params, setup.laps, r.id, &error,
                                        cw.world.get(), o.vehicle);
        EXPECT_TRUE(r.driver) << o.pathFile << ": " << error;
        if (!r.driver)
            continue;
        if (debugLevel() == 1)
            printCourse(r, o.pathFile);
        run.racers.push_back(std::move(r));
    }
    if (run.racers.empty())
        return run;
    std::vector<ai::TrackedCar> cars;
    std::vector<phys::Body*> bodies;
    for (int frame = 0; run.time < maxSeconds; ++frame) {
        cars.clear();
        bodies.clear();
        for (const auto& r : run.racers) {
            // Racers are suspects to the police, as RaceScreen flags them (and
            // so other racers, not ambient traffic, to the planner).
            ai::TrackedCar t = track(*r.vehicle, r.id);
            t.suspect = true;
            cars.push_back(t);
            bodies.push_back(&r.vehicle->sim().body);
        }
        addAmbient(cars, *cw.ai);
        for (auto& r : run.racers) {
            r.driver->setHeld(run.time < 1.0f);
            r.driver->update(kDt, cars);
        }
        cw.step(bodies, run.racers.front().vehicle->sim().body.ics.matrix.m3);
        run.time += kDt;
        if (debugLevel() == 2 && frame % 15 == 0)
            trace(run);
        bool allDone = true;
        for (auto& r : run.racers) {
            const Vec3 pos = r.vehicle->sim().modelMatrix().m3;
            if (frame % 3 == 0)
                r.trail.push_back(pos);
            if (const int laps = r.driver->lapsDone(); laps > r.lapsSeen) {
                r.lapTimes.push_back(run.time - r.lastLapStart);
                r.lastLapStart = run.time;
                r.lapsSeen = laps;
            }
            if (r.driver->finished() && r.finishTime < 0.0f)
                r.finishTime = run.time;
            if (debugLevel() == 3)
                printStuck(r, run.time, pos);
            bool off = false;
            if (!r.driver->finished()) {
                bool below = false;
                const float beyond = beyondCurb(*r.driver, pos, &below);
                off = beyond > 1.0f;
                if (off) {
                    if (!r.offRoad) {
                        r.excursion = {};
                        r.excursion.onFirstLap = r.lapsSeen == 0;
                    }
                    r.excursion.seconds += kDt;
                    r.excursion.worst = std::max(r.excursion.worst, beyond);
                    r.excursion.dropped = r.excursion.dropped || below;
                }
            }
            if (r.offRoad && !off)
                r.endExcursion();
            r.offRoad = off;
            allDone = allDone && r.driver->finished() && r.vehicle->sim().speed() < 1.0f;
        }
        if (allDone)
            break;
    }
    for (auto& r : run.racers)
        if (r.offRoad)
            r.endExcursion();
    return run;
}

void report(const char* title, const RaceRun& run) {
    std::printf("%s (%.1f s simulated)\n", title, run.time);
    for (const auto& r : run.racers) {
        std::printf("  car %d %-9s laps", r.id, r.vehicle->model().baseName.c_str());
        for (float t : r.lapTimes)
            std::printf(" %.1f", t);
        std::printf("  finish %.1f s  line %.0f m  beyond sidewalk worst %.1f m (%.1f s)  "
                    "below a raised road %.1f s  backups %d resets %d\n",
                    r.finishTime, r.driver->course().raceDistance(1), r.worstOffRoad, r.offRoadSeconds,
                    r.belowRoadSeconds, r.driver->backups(), r.driver->resets());
    }
}

void plotRun(const CityWorld& cw, const RaceRun& run, const std::string& name) {
    if (const char* zoom = std::getenv("OPENMM2_AI_ZOOM")) {
        float x = 0, y = 0, z = 0, r = 40;
        if (std::sscanf(zoom, "%f,%f,%f,%f", &x, &y, &z, &r) == 4) {
            const Aabb box{{x - r, y - 4, z - r}, {x + r, y + 4, z + r}};
            Plot plot(box, 0.0f, 8.0f);
            plot.walls(*cw.world, box);
            plot.roads(cw.ai->network());
            for (const auto& rr : run.racers)
                plot.polyline(rr.driver->course().line().points, 0x4060FF);
            int i = 0;
            for (const auto& rr : run.racers)
                plot.polyline(rr.trail, kColours[i++ % 8], 1);
            plot.save("zoom_" + name);
        }
    }
    Aabb box;
    for (const auto& r : run.racers)
        for (const Vec3& p : r.driver->course().line().points)
            box.expand(p);
    Plot plot(box);
    plot.roads(cw.ai->network());
    for (const auto& r : run.racers)
        plot.polyline(r.driver->course().line().points, 0x4060FF);
    int i = 0;
    for (const auto& r : run.racers)
        plot.polyline(r.trail, kColours[i++ % 8], 1);
    plot.save(name);
}

std::optional<game::session::RaceSetup> loadSetup(const city::CityData& city, const vfs::Vfs& vfs,
                                                  game::GameMode mode, int race, int opponents, int laps,
                                                  game::Difficulty difficulty = game::Difficulty::Amateur) {
    game::RaceConfig config;
    config.mode = mode;
    config.city = city.info.mapName;
    config.difficulty = difficulty;
    config.raceIndex = race;
    config.opponents = opponents;
    config.laps = laps;
    return game::session::loadRaceSetup(config, city, vfs);
}

} // namespace

TEST(OpponentRace, LondonCircuitLaps) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto cw = CityWorld::load(vfs, "london", 0.0f);
    ASSERT_TRUE(cw);
    const auto setup = loadSetup(*cw->city, vfs, game::GameMode::Circuit, 0, 7, 3);
    ASSERT_TRUE(setup);
    ASSERT_EQ(setup->opponents.size(), 7u);
    const RaceRun run = runRace(*cw, vfs, *setup, 300.0f);
    report("london circuit0 (amateur), 3 laps", run);
    plotRun(*cw, run, "london_circuit0.png");
    ASSERT_EQ(run.racers.size(), 7u);
    for (const auto& r : run.racers) {
        EXPECT_TRUE(r.driver->finished()) << "car " << r.id;
        ASSERT_GE(r.lapTimes.size(), 3u) << "car " << r.id;
        // Flying laps: plausible average speeds for a city circuit (8..45 m/s).
        const float lap = r.driver->course().length();
        for (std::size_t i = 1; i < r.lapTimes.size(); ++i) {
            EXPECT_GT(r.lapTimes[i], lap / 45.0f) << "car " << r.id;
            EXPECT_LT(r.lapTimes[i], lap / 8.0f) << "car " << r.id;
        }
        // The whole field reaches the first bend together, and a car squeezed
        // in the pack can be pushed wide there (car 7 rides up on car 5, three
        // abreast at 25-30 m/s). Allow that racing incident on the first lap,
        // but not on the flying laps.
        EXPECT_LT(r.offRoadFirstLap, 6.0f) << "car " << r.id << " left the road on the first lap";
        EXPECT_LT(r.offRoadSeconds - r.offRoadFirstLap, 2.0f) << "car " << r.id << " left the road";
        EXPECT_EQ(r.driver->resets(), 0) << "car " << r.id;
        // After the finish it drives on to its destination (the finish row),
        // where aiVehiclePhysics::CalcRoadSpeed brakes it to a stop.
        EXPECT_LT(r.vehicle->sim().speed(), 1.0f) << "car " << r.id;
    }
}

// race1's opponents steer round ambient traffic ([Opponent] avoid flags
// 1 1 0 0; race0's, with 0 0 0 0, plough through it as in MM2), keeping to
// the road and its sidewalks at road level. Its two vpcab racers (MaxThrottle
// 0.75) may drop off the raised road east of the course (paths 404 on): MM2's
// engine gives a cab at 0.75 throttle no torque above ~7580 rpm, under its
// 7650 rpm 1-2 upshift (vehEngine::CalcTorque, vehTransmission::
// ComputeConstants), so it runs at 11.6 m/s in first gear until a bump gets
// it into second, and then at up to 37 m/s in third; MM2's Forward pulls the
// handbrake above 30 m/s at full lock, and ambient cars turn solid when
// touched. That is counted apart (belowRoadSeconds); they still finish.
TEST(OpponentRace, LondonRaceThroughTraffic) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto cw = CityWorld::load(vfs, "london", 1.0f);
    ASSERT_TRUE(cw);
    const auto setup = loadSetup(*cw->city, vfs, game::GameMode::Checkpoint, 1, 6, 0);
    ASSERT_TRUE(setup);
    const RaceRun run = runRace(*cw, vfs, *setup, 300.0f);
    report("london race1 (amateur) with full traffic", run);
    plotRun(*cw, run, "london_race1_traffic.png");
    ASSERT_EQ(run.racers.size(), 6u);
    for (const auto& r : run.racers) {
        EXPECT_TRUE(r.driver->finished()) << "car " << r.id;
        const float line = r.driver->course().raceDistance(0);
        if (r.finishTime > 0.0f) {
            EXPECT_LT(r.finishTime, line / 6.0f) << "car " << r.id << " too slow";
        }
        // A racer knocked aside by a collision at speed (34 m/s on the raised
        // road) can spend a few seconds beyond the sidewalk before it is back.
        EXPECT_LT(r.offRoadSeconds, 6.0f) << "car " << r.id << " left the road";
    }
}

// A car pointed at a wall: aiStuck / vehStuck and aiGoalBackup get it back on
// its line, and it races on.
TEST(OpponentRace, RecoversWhenFacingAWall) {
    MM2_REQUIRE_GAME_DATA();
    const vfs::Vfs& vfs = *test::gameData();
    auto cw = CityWorld::load(vfs, "london", 0.0f);
    ASSERT_TRUE(cw);
    const auto setup = loadSetup(*cw->city, vfs, game::GameMode::Circuit, 0, 1, 1);
    ASSERT_TRUE(setup);
    const auto& o = setup->opponents.at(0);

    auto course = ai::Course::fromOpponentPath(cw->ai->network(), o.path, true);
    ASSERT_TRUE(course);
    // The first wall beside the driving line (probed at bumper height up to
    // 30 m left and right of it): the car is put nose to it.
    float wallDist = 0.0f;
    Mat34 facing;
    for (float s = course->startDistance(); s < course->startDistance() + course->length() && wallDist == 0.0f;
         s += 5.0f) {
        Vec3 dir;
        const Vec3 p = course->pointAt(s, &dir);
        for (float side : {1.0f, -1.0f}) {
            const Vec3 across = Vec3{-dir.z, 0.0f, dir.x}.normalized() * side;
            phys::RayHit hit;
            const Vec3 from = p + Vec3{0, 0.8f, 0};
            if (cw->world->probe(from, from + across * 30.0f, hit) && std::abs(hit.normal.y) < 0.5f) {
                wallDist = hit.t * 30.0f;
                facing = Mat34::rotationY(-std::atan2(across.x, -across.z));
                facing.m3 = hit.position - across * 2.7f;
                facing.m3.y = p.y;
                break;
            }
        }
    }
    ASSERT_GT(wallDist, 0.0f) << "no wall beside the circuit";

    AiRacer r;
    r.vehicle = spawnCar(vfs, *cw->world, o.vehicle, facing);
    ASSERT_TRUE(r.vehicle);
    r.id = 1;
    ai::OpponentSettings settings = ai::OpponentSettings::fromData(o.params, 1);
    settings.world = cw->world.get();
    r.driver = std::make_unique<ai::Opponent>(r.vehicle->sim(), std::move(*course), settings, r.id);

    RaceRun run;
    run.racers.push_back(std::move(r));
    AiRacer& car = run.racers.front();
    float recoveredAt = -1.0f;
    const float startProgress = car.driver->progress();
    for (int frame = 0; run.time < 60.0f; ++frame) {
        const std::vector<ai::TrackedCar> cars{track(*car.vehicle, car.id)};
        car.driver->update(kDt, cars);
        phys::Body* bodies[] = {&car.vehicle->sim().body};
        cw->step(bodies, car.vehicle->sim().body.ics.matrix.m3);
        run.time += kDt;
        if (frame % 3 == 0)
            car.trail.push_back(car.vehicle->sim().modelMatrix().m3);
        if (debugLevel() == 3)
            printStuck(car, run.time, car.vehicle->sim().modelMatrix().m3);
        if (recoveredAt < 0.0f && car.driver->progress() > startProgress + 150.0f)
            recoveredAt = run.time;
    }
    std::printf("facing a wall %.1f m off the line: %d backups, %d resets, 150 m along the line after %.1f s\n",
                wallDist, car.driver->backups(), car.driver->resets(), recoveredAt);
    plotRun(*cw, run, "london_wall_recovery.png");
    EXPECT_GE(car.driver->backups() + car.driver->resets(), 1) << "the car never noticed it was stuck";
    EXPECT_GT(recoveredAt, 0.0f) << "the car did not get going again";
}

namespace {

// A suspect (a player-tuned car driven by an Opponent along a road route,
// flagged as the player) starts 70 m behind a parked cop and drives past it
// at up to `speed` m/s, keeping to `slowSpeed` after `slowAfter` seconds.
struct ChaseOptions {
    float speed = 20.0f;
    float slowAfter = 1e9f;
    float slowSpeed = 0.0f;
    float seconds = 30.0f;
    bool parked = false; // the suspect stays where it starts
    float chaseDistance = 250.0f;
    // 3 s into the chase the suspect is moved this far on along its route
    // (0 = never).
    float jumpAhead = 0.0f;
};

struct ChaseResult {
    bool chased = false;
    ai::PoliceCar::Reason reason = ai::PoliceCar::Reason::None;
    float chaseStarted = -1.0f;
    float caughtAt = -1.0f; // within 15 m of the suspect after it slowed down
    bool closedIn = false;  // apprehended (aiPoliceForce::State 1)
    bool blocked = false;   // got in front and held the suspect's heading (Mirror)
    bool siren = false;
    bool sirenAtEnd = false;
    ai::PoliceCar::Mode modeAtEnd = ai::PoliceCar::Mode::Parked;
    float copSpeedAtEnd = 0.0f;
};

ChaseResult runChase(const char* plotName, const ChaseOptions& opt) {
    ChaseResult result;
    const vfs::Vfs& vfs = *test::gameData();
    auto cw = CityWorld::load(vfs, "london", 0.0f);
    if (!cw)
        return result;
    const auto setup = loadSetup(*cw->city, vfs, game::GameMode::Cruise, -1, 0, 0);
    if (!setup || setup->police.empty())
        return result;
    const ai::RoadNetwork& net = cw->ai->network();

    // The first [Police] car of roam.aimap on a road that runs straight
    // through its post: the suspect starts 70 m behind the cop (out of its
    // view) and drives past it.
    const game::session::PoliceSetup* post = nullptr;
    Vec3 suspectStart, ahead;
    int near = -1, farEnd = -1;
    for (const auto& p : setup->police) {
        phys::RayHit hit;
        const Vec3 fwd = -p.spawn.m2;
        const Vec3 behind = p.spawn.m3 - fwd * 70.0f, front = p.spawn.m3 + fwd * 30.0f;
        if (!ai::locateOnRoads(net, behind).onRoad || !ai::locateOnRoads(net, front).onRoad ||
            std::abs(behind.y - p.spawn.m3.y) > 2.0f || std::abs(front.y - p.spawn.m3.y) > 2.0f ||
            cw->world->probe(behind + Vec3{0, 1, 0}, front + Vec3{0, 1, 0}, hit))
            continue;
        // The intersection ahead of the cop, then one far beyond.
        const int next = ai::nearestIntersection(net, p.spawn.m3 + fwd * 60.0f);
        int best = -1;
        float bestScore = -1e9f;
        for (const auto& n : net.intersections()) {
            const Vec3 d = n.centre - p.spawn.m3;
            const float dist = d.mag();
            if (dist < 300.0f || dist > 600.0f || std::abs(d.y) > 15.0f)
                continue;
            if (const float score = d.dot(fwd) / dist; score > bestScore) {
                bestScore = score;
                best = n.id;
            }
        }
        if (next < 0 || best < 0 || bestScore < 0.7f || ai::findRoute(net, next, best).size() < 2)
            continue;
        post = &p;
        suspectStart = behind;
        ahead = fwd;
        near = next;
        farEnd = best;
        break;
    }
    if (!post)
        return result;

    auto cop = spawnCar(vfs, *cw->world, post->vehicle, post->spawn);
    Mat34 sm = Mat34::rotationY(-std::atan2(ahead.x, -ahead.z));
    sm.m3 = suspectStart;
    auto suspect = spawnCar(vfs, *cw->world, "vpbug", sm, {});
    if (!cop || !suspect)
        return result;

    const auto route = ai::findRoute(net, near, farEnd);
    auto course = ai::Course::build(net, route, suspectStart, std::nullopt, false);
    if (!course)
        return result;
    ai::OpponentSettings ss;
    ss.speedLimit = opt.speed;
    ss.world = cw->world.get();
    ss.resetAfterSeconds = 0.0f;
    ai::Opponent driver(suspect->sim(), std::move(*course), ss, 2);

    ai::PoliceSquad police(net);
    ai::PoliceSettings ps = ai::PoliceSettings::fromData(post->params, opt.chaseDistance);
    police.add(cop->sim(), post->spawn, 1, ps);
    ai::PoliceCar& officer = *police.cars().front();

    std::vector<Vec3> copTrail, suspectTrail;
    float t = 0.0f;
    bool slowed = false;
    for (int frame = 0; t < opt.seconds; ++frame) {
        if (!slowed && t >= opt.slowAfter) {
            driver.setSpeedLimit(opt.slowSpeed);
            slowed = true;
        }
        ai::TrackedCar sc = track(*suspect, 2, true);
        sc.suspect = true;
        sc.isPlayer = true;
        ai::TrackedCar cc = track(*cop, 1);
        cc.isPolice = true;
        const std::vector<ai::TrackedCar> cars{cc, sc};
        if (opt.jumpAhead > 0.0f && result.chased && t > result.chaseStarted + 3.0f && t < result.chaseStarted + 3.0f + kDt) {
            const ai::Course& c = driver.course();
            ai::placeOnCourse(suspect->sim(), c, driver.courseDistance() + opt.jumpAhead, 0.0f, {}, 2, {},
                              cw->world.get());
            driver.reset();
        }
        driver.setHeld(opt.parked);
        driver.update(kDt, cars);
        police.update(kDt, cars, cw->world.get(), true);
        phys::Body* bodies[] = {&cop->sim().body, &suspect->sim().body};
        cw->step(bodies, suspect->sim().body.ics.matrix.m3);
        t += kDt;
        if (frame % 3 == 0) {
            copTrail.push_back(cop->sim().modelMatrix().m3);
            suspectTrail.push_back(suspect->sim().modelMatrix().m3);
        }
        if (officer.mode() == ai::PoliceCar::Mode::Chasing && !result.chased) {
            result.chased = true;
            result.chaseStarted = t;
            result.reason = officer.lastReason();
        }
        result.siren = result.siren || officer.siren();
        result.closedIn = result.closedIn || officer.closingIn();
        result.blocked = result.blocked || officer.blocking();
        const float gap = cop->sim().modelMatrix().m3.dist(suspect->sim().modelMatrix().m3);
        if (slowed && result.caughtAt < 0.0f && gap < 15.0f)
            result.caughtAt = t;
        if (debugLevel() == 2 && frame % 15 == 0)
            std::printf("t %5.1f cop mode %d %s v %4.1f suspect v %4.1f gap %5.1f\n", t,
                        static_cast<int>(officer.mode()), officer.closingIn() ? "apprehend" : "follow",
                        cop->sim().speed(), suspect->sim().speed(), gap);
    }
    result.sirenAtEnd = officer.siren();
    result.modeAtEnd = officer.mode();
    result.copSpeedAtEnd = cop->sim().speed();
    Aabb box;
    for (const Vec3& p : copTrail)
        box.expand(p);
    for (const Vec3& p : suspectTrail)
        box.expand(p);
    Plot plot(box, 60.0f, 4.0f);
    plot.walls(*cw->world, Aabb{{box.min.x - 60, box.min.y - 4, box.min.z - 60}, {box.max.x + 60, box.max.y + 4, box.max.z + 60}});
    plot.roads(net);
    plot.polyline(driver.course().line().points, 0x4060FF);
    plot.polyline(suspectTrail, 0x3CB44B, 1);
    plot.polyline(copTrail, 0xE6194B, 1);
    plot.dot(post->spawn.m3.x, post->spawn.m3.z, 0x000000, 3);
    plot.save(plotName);
    return result;
}

} // namespace

// aiPoliceOfficer::DetectPerpetrator: any player within 75 m in front of a
// cop is pursued (no speeding test in MM2); FollowPerpetrator closes to about
// 12.5 m behind it.
TEST(PoliceChase, ChasesACarPassingInFront) {
    MM2_REQUIRE_GAME_DATA();
    ChaseOptions opt;
    opt.speed = 13.0f; // 29 mph: lawful, chased all the same
    opt.slowAfter = 15.0f;
    opt.slowSpeed = 6.0f;
    opt.seconds = 45.0f;
    const ChaseResult r = runChase("police_chase.png", opt);
    std::printf("chase started %.1f s (reason %d), within 15 m %.1f s, apprehended %d, blocked %d\n",
                r.chaseStarted, static_cast<int>(r.reason), r.caughtAt, r.closedIn ? 1 : 0, r.blocked ? 1 : 0);
    EXPECT_TRUE(r.chased);
    EXPECT_EQ(r.reason, ai::PoliceCar::Reason::PlayerInView);
    EXPECT_TRUE(r.siren);
    EXPECT_GT(r.caughtAt, 0.0f) << "the cop never caught up";
    EXPECT_EQ(r.modeAtEnd, ai::PoliceCar::Mode::Chasing);
}

// The nearest pursuer within 25 m of a suspect doing 10 m/s or more
// apprehends it: gets 12 m ahead and blocks it (aiPoliceOfficer::Block,
// aiVehiclePhysics::Mirror).
TEST(PoliceChase, ApprehendsAMovingSuspect) {
    MM2_REQUIRE_GAME_DATA();
    ChaseOptions opt;
    opt.speed = 15.0f;
    opt.seconds = 40.0f;
    const ChaseResult r = runChase("police_apprehend.png", opt);
    std::printf("chase started %.1f s, apprehended %d, blocked %d\n", r.chaseStarted, r.closedIn ? 1 : 0,
                r.blocked ? 1 : 0);
    EXPECT_TRUE(r.chased);
    EXPECT_TRUE(r.closedIn);
}

TEST(PoliceChase, IgnoresACarBehindIt) {
    MM2_REQUIRE_GAME_DATA();
    // The suspect stays 70 m behind the cop: outside its field of view.
    ChaseOptions opt;
    opt.parked = true;
    opt.seconds = 10.0f;
    const ChaseResult r = runChase("police_behind.png", opt);
    EXPECT_FALSE(r.chased);
    EXPECT_FALSE(r.siren);
}

// Beyond [CopChaseDistance] the suspect escapes (PerpEscapes): siren off,
// the cop stops where it is and watches again.
TEST(PoliceChase, SuspectEscapesBeyondTheChaseDistance) {
    MM2_REQUIRE_GAME_DATA();
    ChaseOptions opt;
    opt.speed = 26.0f;
    opt.seconds = 25.0f;
    opt.jumpAhead = 280.0f;
    opt.chaseDistance = 150.0f; // london crash5's [CopChaseDistance]
    const ChaseResult r = runChase("police_escape.png", opt);
    EXPECT_TRUE(r.chased);
    EXPECT_FALSE(r.sirenAtEnd);
    EXPECT_EQ(r.modeAtEnd, ai::PoliceCar::Mode::Parked);
    EXPECT_LT(r.copSpeedAtEnd, 1.0f);
}

// Every circuit and checkpoint race of both cities, one lap, all opponents.
// Prints one line per race; diagnostics, not a pass/fail test.
TEST(OpponentRace, EveryRaceSweep) {
    MM2_REQUIRE_GAME_DATA();
    if (!std::getenv("OPENMM2_AI_SWEEP"))
        GTEST_SKIP() << "set OPENMM2_AI_SWEEP=1";
    const vfs::Vfs& vfs = *test::gameData();
    const char* only = std::getenv("OPENMM2_AI_ONLY");
    int finished = 0, total = 0;
    for (const char* cityName : {"london", "sf"}) {
        if (only && std::string_view(only).substr(0, std::string_view(only).find(' ')) != cityName)
            continue;
        for (auto mode : {game::GameMode::Circuit, game::GameMode::Checkpoint}) {
            for (auto diff : {game::Difficulty::Amateur, game::Difficulty::Professional}) {
                for (int race = 0; race < 16; ++race) {
                    const std::string key = std::string(cityName) +
                                            (mode == game::GameMode::Circuit ? " circuit" : " race") +
                                            std::to_string(race) + (diff == game::Difficulty::Amateur ? " a" : " p");
                    if (only && key != only)
                        continue;
                    // A fresh city per race (props and traffic as at a race start).
                    auto cw = CityWorld::load(vfs, cityName, 0.0f);
                    ASSERT_TRUE(cw);
                    const auto setup = loadSetup(*cw->city, vfs, mode, race, 8, 1, diff);
                    if (!setup || setup->opponents.empty())
                        continue;
                    float longest = 0.0f;
                    for (const auto& o : setup->opponents)
                        if (auto c = ai::Course::fromOpponentPath(cw->ai->network(), o.path,
                                                                  mode == game::GameMode::Circuit))
                            longest = std::max(longest, c->raceDistance(1));
                    const RaceRun run = runRace(*cw, vfs, *setup, std::max(120.0f, longest / 6.0f + 60.0f));
                    int done = 0, resets = 0, backups = 0, wrecked = 0;
                    float off = 0, slowest = 0, least = 1.0f;
                    for (const auto& r : run.racers) {
                        done += r.driver->finished();
                        resets += r.driver->resets();
                        backups += r.driver->backups();
                        wrecked += r.vehicle->sim().damage.wrecked();
                        off = std::max(off, r.offRoadSeconds);
                        slowest = std::max(slowest, r.finishTime);
                        least = std::min(least, r.driver->progress() / r.driver->course().raceDistance(1));
                    }
                    finished += done;
                    total += static_cast<int>(run.racers.size());
                    std::printf("%s: %d/%zu finished (%d wrecked), slowest %.0f s, line %.0f m, resets %d, "
                                "backups %d, beyond sidewalk %.1f s, least progress %.0f%%\n",
                                key.c_str(), done, run.racers.size(), wrecked, slowest, longest, resets, backups, off,
                                least * 100.0f);
                    std::string file = key;
                    std::replace(file.begin(), file.end(), ' ', '_');
                    plotRun(*cw, run, file + ".png");
                }
            }
        }
    }
    std::printf("sweep: %d/%d opponents finished\n", finished, total);
}
