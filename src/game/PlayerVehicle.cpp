#include "game/PlayerVehicle.h"

#include "asset/Bound.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/DatFile.h"
#include "phys/vehicle/TuneParams.h"

#include <format>

namespace mm2::game {
namespace {

std::optional<data::DatFile> readDat(const vfs::Vfs& vfs, const std::string& path) {
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    std::string error;
    auto f = data::parseDat(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &error);
    if (!f)
        log::warn("vehicle: {}: {}", path, error);
    return f;
}

} // namespace

std::unique_ptr<SimVehicle> SimVehicle::load(const vfs::Vfs& vfs, std::string_view baseIn, std::string* error,
                                             std::string_view tuneSuffix) {
    const std::string base = str::lower(baseIn);
    auto v = std::make_unique<SimVehicle>();
    auto read = [&](std::string_view path) { return vfs.readAll(path); };
    auto model = asset::loadVehicleModel(base, read, error);
    if (!model)
        return nullptr;
    v->m_model = std::move(*model);

    // Tune: the requested variant first (e.g. vpbug_opp), else the base car.
    auto tunePath = [&](std::string_view ext) {
        const std::string variant = std::format("tune/vehicle/{}{}.{}", base, tuneSuffix, ext);
        return tuneSuffix.empty() || !vfs.exists(variant) ? std::format("tune/vehicle/{}.{}", base, ext) : variant;
    };
    phys::CarSimParams params;
    if (auto f = readDat(vfs, tunePath("vehcarsim")); f && f->top()) {
        phys::loadCarSimParams(*f->top(), params);
    } else {
        if (error)
            *error = std::format("missing {}", tunePath("vehcarsim"));
        return nullptr;
    }

    // Geometry: wheels from the model's pivots, body box from its bound.
    phys::VehicleGeometry geom = phys::VehicleGeometry::placeholder();
    for (const auto& w : v->m_model.wheels) {
        phys::WheelGeometry wg{w.position, w.radius, w.width, true};
        if (w.index >= 0 && w.index < 4)
            geom.wheels[static_cast<std::size_t>(w.index)] = wg;
        else if (w.index < 6)
            geom.extraWheels[static_cast<std::size_t>(w.index - 4)] = wg;
    }
    std::optional<asset::BoundGeometry> bound;
    if (auto bin = vfs.readAll("bound/" + base + "_bound.bbnd"))
        bound = asset::parseBbnd(*bin);
    else if (auto txt = vfs.readAll("bound/" + base + "_bound.bnd"))
        bound = asset::parseBnd(std::string_view(reinterpret_cast<const char*>(txt->data()), txt->size()));
    if (bound && bound->bounds().valid()) {
        geom.body = bound->bounds();
        geom.hull = bound->vertices;
    } else if (const auto* body = v->m_model.pkg.findBest("BODY")) {
        geom.body = body->bounds();
        log::warn("vehicle: {} has no collision bound; using the body mesh box", base);
    }

    v->m_sim.init(params, geom);

    // Semi trailer: vehTrailer + dgTrailerJoint tunes, <base>_trailer model.
    // vehCar::Init builds one only for a car with a trailer_hitch pivot.
    auto trailerTune = readDat(vfs, "tune/vehicle/" + base + ".vehtrailer");
    auto jointTune = readDat(vfs, "tune/vehicle/" + base + ".dgtrailerjoint");
    const asset::Mtx* carHitch = v->m_model.pivot("trailer_hitch");
    if (carHitch && trailerTune && trailerTune->top() && jointTune && jointTune->top()) {
        phys::TrailerParams tp;
        phys::TrailerJointParams jp;
        auto trailerModel = asset::loadVehicleModel(base + "_trailer", read, nullptr);
        if (trailerModel && phys::loadTrailerParams(*trailerTune->top(), tp) &&
            phys::loadTrailerJointParams(*jointTune->top(), jp)) {
            phys::TrailerGeometry tg;
            for (int i = 0; i < 4; ++i) {
                if (const auto* pivot = trailerModel->pivot(std::format("twhl{}", i)))
                    tg.wheels[static_cast<std::size_t>(i)] = {pivot->origin, pivot->halfExtent().y,
                                                              pivot->halfExtent().x * 2.0f, true};
            }
            if (auto txt = vfs.readAll("bound/" + base + "_trailer_bound.bnd")) {
                if (auto b = asset::parseBnd(
                        std::string_view(reinterpret_cast<const char*>(txt->data()), txt->size())))
                    tg.body = b->bounds();
            }
            if (!tg.body.valid())
                if (const auto* body = trailerModel->pkg.findBest("TRAILER"))
                    tg.body = body->bounds();
            tg.carHitch = carHitch->origin;
            if (const auto* hitch = trailerModel->pivot("trailer_hitch"))
                tg.trailerHitch = hitch->origin;
            v->m_trailerModel = std::make_unique<asset::VehicleModel>(std::move(*trailerModel));
            v->m_trailer = std::make_unique<phys::Trailer>();
            v->m_trailer->init(tp, jp, tg, v->m_sim);
        }
    }
    if (auto f = readDat(vfs, tunePath("vehgyro")); f && f->top()) {
        phys::GyroParams p;
        if (phys::loadGyroParams(*f->top(), p))
            v->m_sim.setGyroParams(p);
    }
    if (auto f = readDat(vfs, tunePath("vehstuck")); f && f->top()) {
        phys::StuckParams p;
        if (phys::loadStuckParams(*f->top(), p))
            v->m_sim.setStuckParams(p);
    }
    if (auto f = readDat(vfs, tunePath("vehcardamage")); f && f->top()) {
        phys::CarDamageParams p;
        if (phys::loadCarDamageParams(*f->top(), p))
            v->m_sim.setDamageParams(p);
    }
    return v;
}

void SimVehicle::addTo(phys::World& world) {
    world.add(&m_sim.body);
    if (m_trailer)
        m_trailer->addTo(world);
}

void SimVehicle::removeFrom(phys::World& world) {
    if (m_trailer)
        m_trailer->removeFrom(world);
    world.remove(&m_sim.body);
}

void SimVehicle::reset(const Mat34& model) {
    m_sim.reset(model);
    if (m_trailer)
        m_trailer->reset();
    m_controls.reset();
}

VehiclePose SimVehicle::trailerPose() const {
    VehiclePose pose;
    if (!m_trailer)
        return pose;
    pose.body = m_trailer->modelMatrix();
    for (std::size_t i = 0; i < 4; ++i) {
        pose.wheelWorld[i] = m_trailer->wheels[i].matrix;
        pose.wheelValid[i] = true;
    }
    pose.hasWheelWorld = true;
    // vehTrailerInstance::Draw: the tail lights while the tow car brakes over 0.1.
    pose.brakeLights = m_sim.brakes > 0.1f;
    return pose;
}

void SimVehicle::hold(float steering) {
    if (m_controls.swapThrottle || reversing()) {
        m_controls.reset();
        m_sim.trans.setDrive();
    }
    m_sim.setInputs(0.0f, 1.0f, steering, 1.0f);
}

bool SimVehicle::reversing() const { return m_sim.trans.getCurrentGear() < 0; }

VehiclePose SimVehicle::pose() const {
    VehiclePose pose;
    pose.body = m_sim.modelMatrix();
    for (int i = 0; i < 4; ++i) {
        pose.wheelWorld[static_cast<std::size_t>(i)] = m_sim.wheelMatrix(i);
        pose.wheelValid[static_cast<std::size_t>(i)] = m_model.wheel(i) != nullptr;
    }
    // Extra rear wheels follow the rear axle's wheels at their own pivots.
    for (const auto& w : m_model.wheels) {
        if (w.index < 4 || w.index > 5)
            continue;
        const int follow = w.index - 2; // WHL4 follows WHL2, WHL5 follows WHL3
        const auto* lead = m_model.wheel(follow);
        Mat34 m = m_sim.wheelMatrix(follow);
        if (lead)
            m.m3 += pose.body.transformDir(w.position - lead->position);
        pose.wheelWorld[static_cast<std::size_t>(w.index)] = m;
        pose.wheelValid[static_cast<std::size_t>(w.index)] = true;
    }
    pose.hasWheelWorld = true;
    // vehCarModel::DrawGlow: brake input not zero; reverse gear.
    pose.brakeLights = m_sim.brakes != 0.0f;
    pose.reverseLights = reversing();
    return pose;
}

} // namespace mm2::game
