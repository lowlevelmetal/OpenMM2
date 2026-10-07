// AI commands: aidump (road network analysis), aisim (headless traffic sim).
#include "Command.h"
#include "Common.h"
#include "ai/World.h"
#include "asset/Image.h"
#include "city/CityData.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <print>

namespace mm2::tool {
namespace {

std::optional<city::CityData> loadCityArg(const std::string& sourceArg, const std::string& cityName,
                                          vfs::Vfs& v) {
    std::string err;
    auto source = vfs::probeGameSource(str::toPath(sourceArg), &err);
    if (!source || !vfs::mountGameSource(v, *source, &err)) {
        std::println(stderr, "error: {}", err);
        return std::nullopt;
    }
    auto c = city::loadCity(v, cityName, &err);
    if (!c)
        std::println(stderr, "error: {}", err);
    return c;
}

int cmdAiDump(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    vfs::Vfs v;
    auto c = loadCityArg(args[0], args[1], v);
    if (!c || !c->aiMap)
        return 1;
    const auto& map = *c->aiMap;
    std::size_t first = 0, last = map.paths.size();
    if (args.size() > 2) {
        first = static_cast<std::size_t>(str::parseInt(args[2]).value_or(0));
        last = std::min(first + 1, map.paths.size());
    }
    for (std::size_t p = first; p < last; ++p) {
        const auto& path = map.paths[p];
        std::println("path {} flags 0x{:x} halfWidth {:.3f} sections {} rooms {}", path.id, path.flags,
                     path.halfWidth, path.sectionCount(), path.rooms.size());
        for (int e = 0; e < 2; ++e) {
            const auto& end = path.ends[static_cast<std::size_t>(e)];
            std::println("  end{} int {} rule {} roadIdx {} u1 {:x} u2 {} u3 {} light ({:.2f} {:.2f} {:.2f}) "
                         "axis ({:.2f} {:.2f} {:.2f})",
                         e, end.intersection, end.vehicleRule, end.roadIndex, end.unknown1, end.unknown2,
                         end.unknown3, end.trafficLightPos.x, end.trafficLightPos.y, end.trafficLightPos.z,
                         end.trafficLightAxis.x, end.trafficLightAxis.y, end.trafficLightAxis.z);
        }
        for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
            const auto& side = sideIdx == 0 ? path.left : path.right;
            std::println("  {} lanes {} trams {} trains {} sidewalks {} type {} u5 {} u6 {}",
                         sideIdx == 0 ? "L" : "R", side.numLanes, side.numTrams, side.numTrains,
                         side.numSidewalks, side.roadType, side.unknown5, side.unknown6);
            std::string params;
            for (float f : side.params)
                params += std::format(" {:.3g}", f);
            std::println("    params{}", params);
            std::string extras;
            for (float f : side.laneExtras)
                extras += std::format(" {:.3f}", f);
            std::println("    laneExtras{}  endValues{}", extras, [&] {
                std::string s;
                for (float f : side.laneEndValues)
                    s += std::format(" {:.3f}", f);
                return s;
            }());
            for (std::size_t l = 0; l < side.laneLengths.size(); ++l) {
                std::string s;
                for (float f : side.laneLengths[l])
                    s += std::format(" {:.2f}", f);
                std::println("    laneLengths[{}]{}", l, s);
            }
            // Lateral offset of each polyline from the centre, along xAxis, at each section.
            for (std::size_t k = 0; k < side.polylines.size(); ++k) {
                std::string s;
                for (std::size_t sec = 0; sec < side.polylines[k].size() && sec < path.center.size(); ++sec) {
                    const Vec3 d = side.polylines[k][sec] - path.center[sec];
                    s += std::format(" [{:.2f} y{:.2f} z{:.2f}]", d.dot(path.xAxis[sec]), d.y,
                                     d.dot(path.zAxis[sec]));
                }
                std::println("    poly{}{}", k, s);
            }
        }
        std::string cl;
        for (float f : path.centerLengths)
            cl += std::format(" {:.2f}", f);
        std::println("  centerLengths{}", cl);
    }
    return 0;
}

// Minimal RGBA canvas (rows stored bottom-up for asset::encodePng).
struct Canvas {
    int w, h;
    float minX, minZ, scale;
    std::vector<std::uint8_t> px;
    Canvas(int width, int height, float x0, float z0, float s)
        : w(width), h(height), minX(x0), minZ(z0), scale(s) {
        px.assign(static_cast<std::size_t>(w) * h * 4, 0);
        for (std::size_t i = 0; i < px.size(); i += 4)
            px[i + 3] = 255;
    }
    void plot(int x, int y, std::uint32_t c) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        auto* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
        p[0] = c & 0xFF;
        p[1] = (c >> 8) & 0xFF;
        p[2] = (c >> 16) & 0xFF;
    }
    void line(const Vec3& a, const Vec3& b, std::uint32_t c) {
        int x0 = static_cast<int>((a.x - minX) * scale), y0 = static_cast<int>((a.z - minZ) * scale);
        const int x1 = static_cast<int>((b.x - minX) * scale), y1 = static_cast<int>((b.z - minZ) * scale);
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (int i = 0; i < 20000; ++i) {
            plot(x0, y0, c);
            if (x0 == x1 && y0 == y1)
                break;
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
    void dot(const Vec3& p, int r, std::uint32_t c) {
        const int cx = static_cast<int>((p.x - minX) * scale), cy = static_cast<int>((p.z - minZ) * scale);
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x)
                if (x * x + y * y <= r * r)
                    plot(cx + x, cy + y, c);
    }
    void polyline(const ai::Polyline& pl, std::uint32_t c) {
        for (std::size_t i = 1; i < pl.points.size(); ++i)
            line(pl.points[i - 1], pl.points[i], c);
    }
};

constexpr std::uint32_t rgb(int r, int g, int b) {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16);
}

int cmdAiSim(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    float seconds = 60.0f, radius = 150.0f;
    std::string png;
    std::optional<Vec3> at;
    bool drive = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto next = [&]() -> std::string {
            return i + 1 < args.size() ? std::string(args[++i]) : std::string();
        };
        if (a == "--seconds")
            seconds = static_cast<float>(str::parseDouble(next()).value_or(60.0));
        else if (a == "--png")
            png = next();
        else if (a == "--radius")
            radius = static_cast<float>(str::parseDouble(next()).value_or(150.0));
        else if (a == "--drive")
            drive = true;
        else if (a == "--at") {
            const auto parts = str::split(next(), ',');
            if (parts.size() == 2)
                at = Vec3{static_cast<float>(str::parseDouble(parts[0]).value_or(0)), 0,
                          static_cast<float>(str::parseDouble(parts[1]).value_or(0))};
        }
    }
    vfs::Vfs v;
    auto c = loadCityArg(args[0], args[1], v);
    if (!c)
        return 1;
    std::string err;
    ai::Settings settings;
    auto world = ai::World::create(*c, v, settings, nullptr, &err);
    if (!world) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    const auto& net = world->network();
    // Player: a fixed point near the busiest intersection, or a car driving lanes.
    Vec3 player = at.value_or(Vec3{});
    if (!at) {
        std::size_t best = 0;
        for (std::size_t i = 0; i < net.intersections().size(); ++i)
            if (net.intersections()[i].lights.size() > net.intersections()[best].lights.size())
                best = i;
        player = net.intersections()[best].centre + Vec3{0, 0, 1.0f};
    }
    int driveLane = -1;
    float driveS = 0.0f;
    if (drive) {
        float bestD = 1e9f;
        for (const auto& lane : net.lanes()) {
            float d = 0.0f;
            const float s = lane.line.project(player, &d);
            if (d < bestD) {
                bestD = d;
                driveLane = lane.id;
                driveS = s;
            }
        }
    }

    const float minX = player.x - radius, minZ = player.z - radius;
    const int size = static_cast<int>(radius * 2.0f * 2.0f);
    Canvas canvas(size, size, minX, minZ, 2.0f);
    for (const auto& w : net.sidewalks())
        canvas.polyline(w.centre, rgb(60, 60, 70));
    for (const auto& l : net.lanes())
        canvas.polyline(l.line, rgb(110, 110, 110));

    // Run and check.
    const int steps = static_cast<int>(seconds / ai::kAiStepSeconds);
    std::map<int, Vec3> lastCar, lastPed;
    int maxCars = 0, maxPeds = 0, redRuns = 0, overlaps = 0, dives = 0;
    float maxLaneError = 0.0f;
    std::map<int, std::string> lastPedState;
    std::vector<float> redFor(world->signals().size(), 0.0f); // seconds each light has been red
    std::map<int, std::pair<float, int>> commitInfo;          // car -> (time, light state at commit)
    std::vector<int> entriesPerMinute;                        // intersection entries per simulated minute
    std::map<int, float> stillFor;                            // seconds each car has been stationary
    const int traceCar =
        std::getenv("OPENMM2_AISIM_TRACE") ? std::atoi(std::getenv("OPENMM2_AISIM_TRACE")) : -1;
    const bool verbose = std::getenv("OPENMM2_AISIM_VERBOSE") != nullptr;
    for (int i = 0; i < steps; ++i) {
        Vec3 vel;
        if (drive && driveLane >= 0) {
            const float speed = 13.0f;
            driveS += speed * ai::kAiStepSeconds;
            const auto* lane = &net.lanes()[static_cast<std::size_t>(driveLane)];
            if (driveS > lane->line.length) {
                const auto exits = net.exits(lane->toIntersection, lane->path);
                if (exits.empty()) {
                    driveS = lane->line.length;
                } else {
                    driveLane = exits[static_cast<std::size_t>(i) % exits.size()];
                    driveS = 0.0f;
                    lane = &net.lanes()[static_cast<std::size_t>(driveLane)];
                }
            }
            Vec3 dir;
            const Vec3 p = lane->line.pointAt(driveS, &dir);
            vel = dir * speed;
            canvas.line(player, p, rgb(255, 255, 255));
            player = p;
        }
        // Light states before the step, to detect red-light runs.
        std::map<int, bool> wasTurning;
        for (const auto& car : world->cars())
            wasTurning[car.id] = world->traffic().debug(car.id).turning;
        world->step(player, vel);
        for (const auto& car : world->cars()) {
            const auto dbg = world->traffic().debug(car.id);
            if (car.id == traceCar && i % 3 == 0) {
                const auto& lane = net.lanes()[static_cast<std::size_t>(std::max(dbg.lane, 0))];
                std::println("t={:.2f} car {} lane {} len {:.1f} s {:.2f} speed {:.2f} a {:.2f} tv {:.2f} "
                             "react {}/{} lead {} @ {:.1f} turn {} entered {} light {}",
                             i * ai::kAiStepSeconds, car.id, dbg.lane, lane.line.length, dbg.s, car.speed,
                             dbg.accel, dbg.targetVelocity, dbg.reactTicks, dbg.totReactTicks, dbg.lead,
                             dbg.leadDistance, dbg.turning, dbg.entered,
                             lane.lightSlot >= 0 ? static_cast<int>(world->lights().state(lane.lightSlot))
                                                 : -1);
            }
            if (dbg.entered && !commitInfo.contains(car.id) && dbg.lane >= 0) {
                const auto& lane = net.lanes()[static_cast<std::size_t>(dbg.lane)];
                commitInfo[car.id] = {
                    i * ai::kAiStepSeconds,
                    lane.lightSlot >= 0 ? static_cast<int>(world->lights().state(lane.lightSlot)) : 0};
            }
            if (!dbg.entered && !dbg.turning)
                commitInfo.erase(car.id);
            if (dbg.turning && !wasTurning[car.id] && dbg.lane >= 0) {
                const std::size_t minute = static_cast<std::size_t>(i * ai::kAiStepSeconds / 60.0f);
                if (entriesPerMinute.size() <= minute)
                    entriesPerMinute.resize(minute + 1);
                ++entriesPerMinute[minute];
                const auto& lane = net.lanes()[static_cast<std::size_t>(dbg.lane)];
                // Cars that committed on green may finish crossing on amber or
                // early red, as in the original; count entries well into red.
                if (lane.rule == ai::EntryRule::TrafficLight && lane.lightSlot >= 0 &&
                    redFor[static_cast<std::size_t>(lane.lightSlot)] > 2.0f) {
                    ++redRuns;
                    if (verbose)
                        std::println("red run: car {} lane {} slot {} red for {:.1f} s (committed at "
                                     "t={:.1f} with light {})",
                                     car.id, dbg.lane, lane.lightSlot,
                                     redFor[static_cast<std::size_t>(lane.lightSlot)],
                                     commitInfo.contains(car.id) ? commitInfo[car.id].first : -1.0f,
                                     commitInfo.contains(car.id) ? commitInfo[car.id].second : -1);
                }
            }
            if (!dbg.turning && dbg.lane >= 0) {
                float d = 0.0f;
                net.lanes()[static_cast<std::size_t>(dbg.lane)].line.project(car.transform.m3, &d);
                maxLaneError = std::max(maxLaneError, d);
            }
            const auto it = lastCar.find(car.id);
            if (it != lastCar.end() && it->second.dist(car.transform.m3) < 5.0f)
                canvas.line(it->second, car.transform.m3,
                            rgb(80 + (car.id * 53) % 170, 80 + (car.id * 97) % 170, 200));
            lastCar[car.id] = car.transform.m3;
        }
        // Overlaps: cars whose boxes' centres are closer than half their lengths.
        const auto& cars = world->cars();
        for (std::size_t a = 0; a < cars.size(); ++a)
            for (std::size_t b = a + 1; b < cars.size(); ++b)
                if (cars[a].transform.m3.dist(cars[b].transform.m3) < 1.5f) {
                    ++overlaps;
                    if (verbose) {
                        const auto da = world->traffic().debug(cars[a].id),
                                   db = world->traffic().debug(cars[b].id);
                        std::println("overlap t={:.2f}: car {} lane {} next {} s {:.1f} turn {} / car {} "
                                     "lane {} next {} s {:.1f} turn {}",
                                     i * ai::kAiStepSeconds, cars[a].id, da.lane, da.nextLane, da.s,
                                     da.turning, cars[b].id, db.lane, db.nextLane, db.s, db.turning);
                    }
                }
        for (const auto& p : world->peds()) {
            const auto it = lastPed.find(p.id);
            if (it != lastPed.end() && it->second.dist(p.transform.m3) < 3.0f)
                canvas.line(it->second, p.transform.m3, rgb(60, 220, 90));
            lastPed[p.id] = p.transform.m3;
            if (p.state.find("DIVE") != std::string::npos &&
                lastPedState[p.id].find("DIVE") == std::string::npos)
                ++dives;
            lastPedState[p.id] = p.state;
        }
        for (std::size_t k = 0; k < redFor.size(); ++k)
            redFor[k] =
                world->signals()[k].state == ai::LightState::Red ? redFor[k] + ai::kAiStepSeconds : 0.0f;
        for (const auto& car : world->cars())
            stillFor[car.id] = car.speed < 0.1f ? stillFor[car.id] + ai::kAiStepSeconds : 0.0f;
        if (verbose && i + 1 == steps) {
            for (const auto& car : world->cars()) {
                if (stillFor[car.id] < 120.0f)
                    continue;
                const auto d = world->traffic().debug(car.id);
                const auto& lane = net.lanes()[static_cast<std::size_t>(std::max(d.lane, 0))];
                std::println("stuck {:.0f}s: car {} lane {} next {} s {:.1f}/{:.1f} turn {} entered {} rule "
                             "{} light {} lead {} @ {:.1f} ({})",
                             stillFor[car.id], car.id, d.lane, d.nextLane, d.s, lane.line.length, d.turning,
                             d.entered, static_cast<int>(lane.rule),
                             lane.lightSlot >= 0 ? static_cast<int>(world->lights().state(lane.lightSlot))
                                                 : -1,
                             d.lead, d.leadDistance,
                             d.lead >= 0 ? std::format("{:.0f}s", stillFor[d.lead]) : std::string("-"));
            }
        }
        maxCars = std::max(maxCars, static_cast<int>(world->cars().size()));
        if (verbose && i % 300 == 0)
            std::println("cars at t={:.0f}: {}", i * ai::kAiStepSeconds, world->cars().size());
        maxPeds = std::max(maxPeds, static_cast<int>(world->peds().size()));
    }
    for (const auto& s : world->signals()) {
        const std::uint32_t col = s.state == ai::LightState::Green   ? rgb(0, 255, 0)
                                  : s.state == ai::LightState::Amber ? rgb(255, 190, 0)
                                                                     : rgb(255, 0, 0);
        canvas.dot(s.transform.m3, 2, col);
    }
    for (const auto& car : world->cars())
        canvas.dot(car.transform.m3, 2, rgb(255, 255, 0));
    canvas.dot(player, 4, rgb(255, 0, 255));

    std::println("{} s simulated: max {} cars, {} pedestrians; {} red-light runs, {} overlapping car pairs "
                 "(car-steps),"
                 " max lane deviation {:.2f} m, {} pedestrian dives",
                 seconds, maxCars, maxPeds, redRuns, overlaps, maxLaneError, dives);
    int stoppedAtEnd = 0;
    std::map<std::string, int> why;
    for (const auto& car : world->cars()) {
        if (car.speed >= 0.1f)
            continue;
        ++stoppedAtEnd;
        const auto d = world->traffic().debug(car.id);
        std::string key;
        if (d.turning) {
            key = "in turn";
        } else {
            const auto& lane = net.lanes()[static_cast<std::size_t>(d.lane)];
            const float toEnd = lane.line.length - d.s;
            const char* rule = lane.rule == ai::EntryRule::TrafficLight ? "light"
                               : lane.rule == ai::EntryRule::StopSign   ? "stop sign"
                                                                        : "uncontrolled";
            if (toEnd < 4.0f)
                key = std::format(
                    "at line ({}{}{})", rule,
                    lane.lightSlot >= 0
                        ? std::format(" state {}", static_cast<int>(world->lights().state(lane.lightSlot)))
                        : "",
                    d.entered ? ", entered" : (d.nextLane < 0 ? ", no next" : ""));
            else
                key = d.lead >= 0 && d.leadDistance < 20.0f ? "queued behind lead" : "stopped mid-lane";
        }
        ++why[key];
    }
    for (const auto& [k, n] : why)
        std::println("  {:3} {}", n, k);
    std::println("{} of {} cars stopped at the end", stoppedAtEnd, world->cars().size());
    std::string perMinute;
    for (int n : entriesPerMinute)
        perMinute += std::format(" {}", n);
    std::println("intersection entries per minute:{}", perMinute);
    if (!png.empty()) {
        asset::Image::Level level{static_cast<std::uint32_t>(canvas.w), static_cast<std::uint32_t>(canvas.h),
                                  canvas.px};
        const auto bytes = asset::encodePng(level);
        if (!file::writeAtomic(str::toPath(png), bytes)) {
            std::println(stderr, "error: cannot write {}", png);
            return 1;
        }
        std::println("wrote {}", png);
    }
    return 0;
}

const Registrar r1({"aidump", "<container> <city> [path]",
                    "dump AI road network paths (lanes, polylines, ends)", &cmdAiDump});
const Registrar r2({"aisim",
                    "<container> <city> [--seconds N] [--png out.png] [--at x,z] [--radius R] [--drive]",
                    "run the ambient traffic and pedestrians headless and plot them", &cmdAiSim});

} // namespace
} // namespace mm2::tool
