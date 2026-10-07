// Physics commands: simcar, simcars.
//
// simcar drives one car on a flat test plane and prints its speed, engine
// RPM and gear over time; simcars runs the standard acceleration test for
// every player car. Car geometry comes straight from the retail files:
// wheel pivots from geometry/<car>_whlN.mtx (12 floats: bounding-box min and
// max rows, an unused row, then the pivot position) and the body box from the
// vertices of bound/<car>_bound.bnd. These two readers are deliberately
// minimal stopgaps until the asset module provides them.

#include "Command.h"
#include "Common.h"
#include "asset/Pkg.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"
#include "data/TextTables.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/Trailer.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <print>

namespace mm2::tool {
namespace {

using namespace mm2::phys;

std::optional<std::string> readText(const vfs::FileSystem& fs, const std::string& path) {
    auto f = fs.open(path);
    if (!f)
        return std::nullopt;
    auto bytes = f->readAll();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::optional<data::DatFile> readDat(const vfs::FileSystem& fs, const std::string& path) {
    auto text = readText(fs, path);
    if (!text)
        return std::nullopt;
    std::string err;
    auto dat = data::parseDat(*text, &err);
    if (!dat)
        std::println(stderr, "warning: {}: {}", path, err);
    return dat;
}

std::optional<Mat34> readMtx(const vfs::FileSystem& fs, const std::string& path) {
    auto f = fs.open(path);
    if (!f || f->size() < 48)
        return std::nullopt;
    std::byte buf[48];
    if (!f->readExact(0, buf))
        return std::nullopt;
    Mat34 m;
    for (int r = 0; r < 4; ++r)
        m.row(r) = {loadLE<float>(buf + r * 12), loadLE<float>(buf + r * 12 + 4),
                    loadLE<float>(buf + r * 12 + 8)};
    return m;
}

Aabb readBoundBox(const vfs::FileSystem& fs, const std::string& path) {
    Aabb box;
    auto text = readText(fs, path);
    if (!text)
        return box;
    for (std::string_view line : data::splitLines(*text)) {
        line = str::trim(line);
        if (line.size() < 2 || line[0] != 'v' || (line[1] != ' ' && line[1] != '\t'))
            continue;
        float v[3];
        int n = 0;
        for (auto tok : str::split(str::trim(line.substr(1)), '\t')) {
            for (auto t : str::split(tok, ' ')) {
                if (auto d = str::parseDouble(t); d && n < 3)
                    v[n++] = static_cast<float>(*d);
            }
        }
        if (n == 3)
            box.expand(Vec3{v[0], v[1], v[2]});
    }
    return box;
}

struct LoadedCar {
    CarSimParams params;
    GyroParams gyro;
    StuckParams stuck;
    CarDamageParams damage;
    VehicleGeometry geometry;
    bool realGeometry = false;
    bool hasTrailer = false;
    TrailerParams trailer;
    TrailerJointParams joint;
    TrailerGeometry trailerGeometry;
    Aabb trailerMesh; // TRAILER (H) mesh box, for --trailer-cg mesh
};

std::optional<LoadedCar> loadCar(const vfs::FileSystem& fs, const std::string& car) {
    LoadedCar out;
    auto sim = readDat(fs, "tune/vehicle/" + car + ".vehcarsim");
    if (!sim || !sim->top() || !loadCarSimParams(*sim->top(), out.params)) {
        std::println(stderr, "error: cannot load tune/vehicle/{}.vehCarSim", car);
        return std::nullopt;
    }
    if (auto d = readDat(fs, "tune/vehicle/" + car + ".vehgyro"); d && d->top())
        loadGyroParams(*d->top(), out.gyro);
    if (auto d = readDat(fs, "tune/vehicle/" + car + ".vehstuck"); d && d->top())
        loadStuckParams(*d->top(), out.stuck);
    if (auto d = readDat(fs, "tune/vehicle/" + car + ".vehcardamage"); d && d->top())
        loadCarDamageParams(*d->top(), out.damage);

    // Geometry: strip "_opp" style suffixes to find the model.
    std::string model = car;
    if (auto us = model.find('_'); us != std::string::npos)
        model = model.substr(0, us);
    out.geometry = VehicleGeometry::placeholder();
    int found = 0;
    for (int i = 0; i < 4; ++i) {
        if (auto m = readMtx(fs, std::format("geometry/{}_whl{}.mtx", model, i))) {
            out.geometry.wheels[static_cast<std::size_t>(i)] = VehicleGeometry::wheelFromPivot(*m);
            ++found;
        }
    }
    const Aabb body = readBoundBox(fs, "bound/" + model + "_bound.bnd");
    if (body.valid())
        out.geometry.body = body;
    out.realGeometry = found == 4 && body.valid();

    // Semi trailers (vpsemi, vpcentury).
    auto trailer = readDat(fs, "tune/vehicle/" + car + ".vehtrailer");
    auto joint = readDat(fs, "tune/vehicle/" + car + ".dgtrailerjoint");
    if (trailer && trailer->top() && joint && joint->top() &&
        loadTrailerParams(*trailer->top(), out.trailer) && loadTrailerJointParams(*joint->top(), out.joint)) {
        out.hasTrailer = true;
        for (int i = 0; i < 4; ++i) {
            auto m = readMtx(fs, std::format("geometry/{}_trailer_twhl{}.mtx", model, i));
            out.trailerGeometry.wheels[static_cast<std::size_t>(i)] =
                m ? VehicleGeometry::wheelFromPivot(*m)
                  : WheelGeometry{{i % 2 ? 1.0f : -1.0f, 0.5f, i < 2 ? 2.0f : 4.0f}, 0.5f, 0.3f, true};
        }
        out.trailerGeometry.body = readBoundBox(fs, "bound/" + model + "_trailer_bound.bnd");
        if (auto bytes = readFile(fs, "geometry/" + model + "_trailer.pkg"))
            if (auto pkg = asset::parsePkg(*bytes, nullptr))
                if (const auto* mesh = pkg->findBest("TRAILER"))
                    out.trailerMesh = mesh->bounds();
    }
    return out;
}

// A 100 km square of flat road.
World makeTestWorld(const vfs::FileSystem& fs) {
    MaterialTable materials;
    if (auto mtl = readText(fs, "city/materials.mtl"))
        if (auto list = parseMaterials(*mtl))
            materials.add(*list);
    World world(materials);
    BoundGeometry ground;
    const float s = 50000.0f;
    ground.vertices = {{-s, 0, -s}, {-s, 0, s}, {s, 0, s}, {s, 0, -s}};
    ground.polys.push_back({{0, 1, 2, 3}, 4, 0});
    ground.materialNames = {"_default"};
    PolygonSoup soup;
    soup.add(ground, Mat34::identity(), world.materials());
    soup.finalize(4096.0f);
    world.setStatic(std::move(soup));
    return world;
}

struct RunResult {
    float zeroTo60 = -1;
    float topSpeedMph = 0;
    float quarterMileTime = -1;
    // Trailer diagnostics (max over the run).
    float maxHitchGap = 0;
    float maxHitchAngle = 0; // |Joint3Dof roll| (yaw articulation), rad
    float maxLean = 0;       // Joint3Dof lean (relative tilt), rad
    float minTrailerUp = 1;  // trailer up . world up
};

struct RunOptions {
    float seconds = 30.0f;
    float throttle = 1.0f;
    float steer = 0.0f;
    float brakeAt = -1.0f;
    float step = kFixedSampleStep;
    bool print = true;
    bool debug = false;
    std::string csv;
    // Trailer centre of gravity: "origin" (model origin, the default),
    // "mesh" (TRAILER mesh box centre, MM1's choice) or "x,y,z".
    std::string trailerCg = "origin";
    bool noTrailer = false;
};

RunResult runCar(const vfs::FileSystem& fs, const LoadedCar& car, const RunOptions& opt) {
    World world = makeTestWorld(fs);
    CarSim sim;
    sim.init(car.params, car.geometry);
    sim.setStuckParams(car.stuck);
    sim.setGyroParams(car.gyro);
    sim.setDamageParams(car.damage);
    sim.reset(Mat34::identity());
    world.add(&sim.body);
    std::unique_ptr<Trailer> trailer;
    if (car.hasTrailer && !opt.noTrailer) {
        TrailerGeometry tg = car.trailerGeometry;
        if (opt.trailerCg == "mesh" && car.trailerMesh.valid()) {
            tg.centerOfGravity = car.trailerMesh.center();
        } else if (opt.trailerCg != "origin") {
            const auto parts = str::split(opt.trailerCg, ',');
            if (parts.size() == 3)
                tg.centerOfGravity = {static_cast<float>(str::parseDouble(parts[0]).value_or(0.0)),
                                      static_cast<float>(str::parseDouble(parts[1]).value_or(0.0)),
                                      static_cast<float>(str::parseDouble(parts[2]).value_or(0.0))};
        }
        if (opt.print)
            std::println("trailer CG ({:.3f} {:.3f} {:.3f})", tg.centerOfGravity.x, tg.centerOfGravity.y,
                         tg.centerOfGravity.z);
        trailer = std::make_unique<Trailer>();
        trailer->init(car.trailer, car.joint, tg, sim);
        trailer->addTo(world);
    }
    ArcadeControls controls;

    std::ofstream csv;
    if (!opt.csv.empty()) {
        csv.open(opt.csv);
        csv << "t,speed_mph,rpm,gear,throttle,x,y,z,yaw_rate\n";
    }
    if (opt.print)
        std::println("{:>6} {:>8} {:>7} {:>5} {:>8}", "t(s)", "mph", "rpm", "gear", "dist(m)");

    RunResult res;
    const Vec3 start = sim.modelMatrix().m3;
    // Settle on the suspension first (no input).
    for (int i = 0; i < static_cast<int>(1.0f / opt.step); ++i) {
        controls.apply(sim, {});
        world.step(opt.step);
    }
    const int samples = static_cast<int>(opt.seconds / opt.step);
    float nextPrint = 0.0f;
    for (int i = 0; i <= samples; ++i) {
        const float t = static_cast<float>(i) * opt.step;
        PedalInput in;
        const bool braking = opt.brakeAt >= 0.0f && t >= opt.brakeAt;
        in.accelerator = braking ? 0.0f : opt.throttle;
        in.brake = braking ? 1.0f : 0.0f;
        in.steering = opt.steer;
        controls.apply(sim, in);
        world.step(opt.step);

        const float mph = sim.speedMph();
        const Vec3 pos = sim.modelMatrix().m3;
        const float dist = (pos - start).mag();
        res.topSpeedMph = std::max(res.topSpeedMph, mph);
        if (res.zeroTo60 < 0 && mph >= 60.0f)
            res.zeroTo60 = t;
        if (res.quarterMileTime < 0 && dist >= 402.336f)
            res.quarterMileTime = t;
        if (csv)
            csv << std::format("{:.4f},{:.3f},{:.1f},{},{:.2f},{:.3f},{:.3f},{:.3f},{:.4f}\n", t, mph,
                               sim.engine.rpm, sim.trans.getCurrentGear(), sim.engine.throttle, pos.x, pos.y,
                               pos.z, sim.body.ics.angularVelocity.y);
        if (trailer) {
            res.maxHitchGap = std::max(res.maxHitchGap, trailer->hitchGap());
            res.maxHitchAngle = std::max(res.maxHitchAngle, std::abs(trailer->joint.roll));
            res.maxLean = std::max(res.maxLean, trailer->joint.lean);
            res.minTrailerUp = std::min(res.minTrailerUp, trailer->body.ics.matrix.m1.y);
        }
        if (opt.print && t >= nextPrint) {
            std::println("{:6.2f} {:8.2f} {:7.0f} {:5} {:8.1f}", t, mph, sim.engine.rpm,
                         sim.trans.getCurrentGear(), dist);
            if (trailer) {
                const InertialCS& ti = trailer->body.ics;
                const float ke = 0.5f * ti.mass * ti.linearVelocity.mag2() +
                                 0.5f * sim.body.ics.mass * sim.body.ics.linearVelocity.mag2();
                std::println("       trailer gap {:.4f} m  hitch {:6.1f} deg  lean {:5.2f} deg  up.y {:.4f}  "
                             "tractor up.y {:.4f}  KE {:.0f} kJ",
                             trailer->hitchGap(), trailer->joint.roll * 57.29578f,
                             trailer->joint.lean * 57.29578f, ti.matrix.m1.y, sim.body.ics.matrix.m1.y,
                             ke * 0.001f);
            }
            if (opt.debug) {
                const auto& ics = sim.body.ics;
                std::println(
                    "       pos ({:.2f} {:.2f} {:.2f}) up ({:.2f} {:.2f} {:.2f}) brakes {:.2f} torque {:.0f}",
                    ics.matrix.m3.x, ics.matrix.m3.y, ics.matrix.m3.z, ics.matrix.m1.x, ics.matrix.m1.y,
                    ics.matrix.m1.z, sim.brakes, sim.engine.torque);
                if (trailer)
                    for (const auto& w : trailer->wheels)
                        std::println(
                            "       trailer wheel centre ({:.2f} {:.2f} {:.2f}) r {:.3f} hit {} susp {:6.3f} "
                            "load {:7.0f} rot {:8.2f} roll {:8.2f} dLat {:7.4f} gLat {:7.0f}",
                            w.center.x, w.center.y, w.center.z, w.radius, w.hit, w.suspension, w.currentLoad,
                            w.rotationSpeed, w.rollingRotation, w.currentTireDispLat, w.tireGripLat);
                for (const auto& w : sim.wheels)
                    std::println(
                        "       hit {} susp {:6.3f} load {:7.0f} rot {:8.2f} roll {:8.2f} dLong {:7.4f} "
                        "gLong {:7.0f} dLat {:7.4f} gLat {:7.0f} sl {:5.2f}",
                        w.hit, w.suspension, w.currentLoad, w.rotationSpeed, w.rollingRotation,
                        w.currentTireDispLong, w.tireGripLong, w.currentTireDispLat, w.tireGripLat,
                        w.longSlipPercent);
            }
            nextPrint += 0.5f;
        }
    }
    return res;
}

bool parseFloatArg(std::span<char* const> args, std::size_t& i, float& out) {
    if (i + 1 >= args.size())
        return false;
    auto v = str::parseDouble(args[++i]);
    if (!v)
        return false;
    out = static_cast<float>(*v);
    return true;
}

int cmdSimcar(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    RunOptions opt;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view a = args[i];
        bool ok = true;
        if (a == "--seconds")
            ok = parseFloatArg(args, i, opt.seconds);
        else if (a == "--throttle")
            ok = parseFloatArg(args, i, opt.throttle);
        else if (a == "--steer")
            ok = parseFloatArg(args, i, opt.steer);
        else if (a == "--brake-at")
            ok = parseFloatArg(args, i, opt.brakeAt);
        else if (a == "--step")
            ok = parseFloatArg(args, i, opt.step);
        else if (a == "--debug")
            opt.debug = true;
        else if (a == "--csv" && i + 1 < args.size())
            opt.csv = args[++i];
        else if (a == "--trailer-cg" && i + 1 < args.size())
            opt.trailerCg = args[++i];
        else if (a == "--no-trailer")
            opt.noTrailer = true;
        else
            ok = false;
        if (!ok) {
            std::println(stderr, "error: bad option '{}'", a);
            return 2;
        }
    }
    auto car = loadCar(*fs, str::lower(args[1]));
    if (!car)
        return 1;
    if (!car->realGeometry)
        std::println(stderr, "note: using placeholder geometry for {}", args[1]);
    const auto r = runCar(*fs, *car, opt);
    std::println("0-60 mph: {}  top speed: {:.1f} mph  quarter mile: {}",
                 r.zeroTo60 >= 0 ? std::format("{:.2f} s", r.zeroTo60) : "n/a", r.topSpeedMph,
                 r.quarterMileTime >= 0 ? std::format("{:.2f} s", r.quarterMileTime) : "n/a");
    if (car->hasTrailer && !opt.noTrailer)
        std::println("trailer: max hitch gap {:.4f} m  max hitch angle {:.1f} deg  max lean {:.2f} deg  "
                     "min up.y {:.4f}",
                     r.maxHitchGap, r.maxHitchAngle * 57.29578f, r.maxLean * 57.29578f, r.minTrailerUp);
    return 0;
}

int cmdSimcars(std::span<char* const> args) {
    if (args.empty())
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    RunOptions opt;
    opt.seconds = args.size() > 1 ? static_cast<float>(str::parseDouble(args[1]).value_or(60.0)) : 60.0f;
    opt.print = false;
    auto list = readText(*fs, "tune/cars.txt");
    if (!list) {
        std::println(stderr, "error: tune/cars.txt not found");
        return 1;
    }
    std::println(
        "| car | drive | mass | hp | High (mph) | 0-60 (s) | 1/4 mile (s) | top (mph) | .info Top Speed |");
    std::println("|---|---|---|---|---|---|---|---|---|");
    for (std::string_view line : data::splitLines(*list)) {
        std::string name = str::lower(str::trim(line));
        if (name.ends_with(".info"))
            name.resize(name.size() - 5);
        if (name.empty())
            continue;
        auto car = loadCar(*fs, name);
        if (!car)
            continue;
        const auto r = runCar(*fs, *car, opt);
        std::string infoTop = "?";
        if (auto info = readText(*fs, "tune/" + name + ".info"))
            infoTop = data::KeyValueFile::parse(*info).getString("Top Speed", "?");
        static const char* kDrive[] = {"RWD", "FWD", "4WD"};
        std::println("| {} | {} | {:.0f} | {:.0f} | {:.0f} | {} | {} | {:.1f} | {} |", name,
                     kDrive[std::clamp(car->params.drivetrainType, 0, 2)], car->params.mass,
                     car->params.engine.maxHorsePower, car->params.trans.high,
                     r.zeroTo60 >= 0 ? std::format("{:.2f}", r.zeroTo60) : "n/a",
                     r.quarterMileTime >= 0 ? std::format("{:.2f}", r.quarterMileTime) : "n/a", r.topSpeedMph,
                     infoTop);
    }
    return 0;
}

const Registrar
    r1({"simcar",
        "<container> <car> [--seconds N] [--throttle T] [--steer S] [--brake-at T] [--step S] [--csv FILE] "
        "[--trailer-cg origin|mesh|x,y,z] [--no-trailer] [--debug]",
        "drive a car on a flat plane and print speed/RPM/gear", &cmdSimcar});
const Registrar r2({"simcars", "<container> [seconds]",
                    "acceleration test for every player car (markdown table)", &cmdSimcars});

} // namespace
} // namespace mm2::tool
