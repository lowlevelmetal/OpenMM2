#include "ai/World.h"

#include "asset/Ped.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <algorithm>
#include <format>

namespace mm2::ai {
namespace {

// Model suffix of a city's own assets: "_l" London, "_f" San Francisco
// ("Frisco"); "_s" models are shared. Inferred from the file names.
std::string citySuffix(const city::CityData& city) {
    return str::istartsWith(city.info.mapName, "london") ? "_l" : "_f";
}

std::string_view asText(const std::vector<std::byte>& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

} // namespace

std::vector<city::AiAmbientType> defaultAmbientTypes(const vfs::Vfs& vfs, const city::CityData& city) {
    const std::string suffix = citySuffix(city);
    std::vector<std::string> models;
    for (const auto& e : vfs.listFiles()) {
        if (!e.path.starts_with("tune/vehicle/va_") || !e.path.ends_with(".aivehicledata"))
            continue;
        std::string model = e.path.substr(13, e.path.size() - 13 - 14);
        if (model.find("cablecar") != std::string::npos)
            continue; // rides the tram rails
        if (model.ends_with(suffix) || model.ends_with("_s"))
            if (vfs.exists("geometry/" + model + ".pkg"))
                models.push_back(model);
    }
    std::ranges::sort(models);
    std::vector<city::AiAmbientType> types;
    for (std::size_t i = 0; i < models.size(); ++i)
        types.push_back({models[i], static_cast<float>(i + 1) / static_cast<float>(models.size()), 0.0f});
    return types;
}

std::vector<PedTypeInfo> loadPedTypes(const vfs::Vfs& vfs) {
    std::vector<std::string> paths;
    for (const auto& e : vfs.listFiles())
        if (e.path.starts_with("anim/"))
            paths.push_back(e.path);
    std::vector<PedTypeInfo> types;
    for (const auto& name : asset::findPedTypes(paths)) {
        auto csv = vfs.readAll("anim/" + name + ".csv");
        if (!csv)
            continue;
        std::string error;
        auto table = asset::parsePedAnimTable(asText(*csv), &error);
        if (!table || !table->find("WALK")) {
            log::warn("ai: pedestrian type {}: {}", name, table ? "no WALK state" : error);
            continue;
        }
        PedTypeInfo info;
        info.name = name;
        info.table = std::move(*table);
        if (auto shaders = vfs.readAll("anim/" + name + ".shaders"))
            if (auto set = asset::parsePedShaders(*shaders))
                info.variants = std::max<int>(1, static_cast<int>(set->variantCount));
        types.push_back(std::move(info));
    }
    return types;
}

std::unique_ptr<World> World::create(const city::CityData& city, const vfs::Vfs& vfs,
                                     const Settings& settings, const city::AiMapConfig* config,
                                     std::string* error) {
    if (!city.aiMap) {
        if (error)
            *error = "city has no AI map (.bai)";
        return nullptr;
    }
    if (!config)
        config = city.cruise ? &*city.cruise : nullptr;

    std::unique_ptr<World> world(new World());
    NetworkOptions net;
    // Drive on the left when the city's cruise map says so; race maps of the
    // same city don't repeat the section (inferred to be city-wide).
    auto dol = [](const std::optional<city::AiMapConfig>& c) {
        return c && c->driveOnLeft && *c->driveOnLeft != 0;
    };
    net.driveOnLeft = (config && config->driveOnLeft && *config->driveOnLeft != 0) || dol(city.cruise) ||
                      dol(city.cruisePro);
    if (config) {
        if (config->speedLimit)
            net.defaultSpeedLimit = *config->speedLimit;
        net.exceptions = config->exceptions;
    }
    world->m_network = std::make_unique<RoadNetwork>(RoadNetwork::build(*city.aiMap, net));
    world->m_lights.build(*world->m_network);

    TrafficSettings traffic;
    traffic.mapDensity = config && config->density ? *config->density : 0.1f;
    traffic.densityScale = settings.trafficDensity;
    traffic.maxCars = settings.maxCars;
    traffic.types =
        config && !config->ambientTypes.empty() ? config->ambientTypes : defaultAmbientTypes(vfs, city);
    std::vector<VehicleData> data;
    for (const auto& t : traffic.types) {
        if (std::ranges::any_of(data, [&](const VehicleData& d) { return str::iequals(d.model, t.model); }))
            continue;
        std::string err;
        if (auto d = loadVehicleData(vfs, t.model, &err))
            data.push_back(std::move(*d));
        else
            log::warn("ai: {}", err);
    }
    world->m_traffic = std::make_unique<Traffic>(*world->m_network, world->m_lights, std::move(data), traffic,
                                                 settings.seed);

    PedSettings peds;
    peds.density = 0.5f * settings.pedestrianDensity;
    peds.maxPeds = settings.maxPeds;
    world->m_peds =
        std::make_unique<Pedestrians>(*world->m_network, loadPedTypes(vfs), peds, settings.seed * 7919u + 1u);

    // Traffic light poles: one per controlled approach.
    const std::string suffix = citySuffix(city);
    for (const auto& site : world->m_network->lights()) {
        Signal s;
        int lanes = 0;
        if (site.lane >= 0) {
            const Lane& lane = world->m_network->lanes()[static_cast<std::size_t>(site.lane)];
            for (int l : world->m_network->paths()[static_cast<std::size_t>(lane.path)].lanes)
                if (world->m_network->lanes()[static_cast<std::size_t>(l)].side == lane.side)
                    ++lanes;
        }
        // Wide approaches get the arm (dual) model where the city has one (inferred).
        const std::string dual = "sp_traflitdual" + suffix;
        s.model = lanes >= 2 && vfs.exists("geometry/" + dual + ".pkg") ? dual : "sp_traflitsingle" + suffix;
        // trafficLightAxis - trafficLightPos points from the pole towards the
        // road; the models extend their arm along -X and light up +Z.
        Vec3 inward{site.facing.x, 0.0f, site.facing.z};
        inward = inward.mag2() > 1e-8f ? inward.normalized() : Vec3{1, 0, 0};
        Mat34 m;
        m.m0 = -inward;
        m.m1 = Vec3::yAxis();
        m.m2 = m.m0.cross(m.m1);
        m.m3 = site.position;
        s.transform = m;
        world->m_signals.push_back(std::move(s));
    }
    world->updateSignals();
    log::info("ai: {} lanes, {} sidewalks, {} intersections, {} lights, drive on the {}",
              world->m_network->lanes().size(), world->m_network->sidewalks().size(),
              world->m_network->intersections().size(), world->m_network->lights().size(),
              net.driveOnLeft ? "left" : "right");
    return world;
}

void World::step(const Vec3& playerPos, const Vec3& playerVel) {
    m_lights.update(kAiStepSeconds);
    m_traffic->step(kAiStepSeconds, playerPos, playerVel);
    m_peds->step(kAiStepSeconds, playerPos, playerVel);
    updateSignals();
}

void World::update(float dt, const Vec3& playerPos, const Vec3& playerVel) {
    m_accumulator += dt;
    int steps = 0;
    while (m_accumulator >= kAiStepSeconds && steps < 8) {
        step(playerPos, playerVel);
        m_accumulator -= kAiStepSeconds;
        ++steps;
    }
    if (steps == 8)
        m_accumulator = 0.0f; // too far behind: drop time rather than spiral
}

void World::updateSignals() {
    for (std::size_t i = 0; i < m_signals.size(); ++i)
        m_signals[i].state = m_lights.state(static_cast<int>(i));
}

} // namespace mm2::ai
