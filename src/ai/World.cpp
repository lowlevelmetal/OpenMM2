#include "ai/World.h"

#include "asset/Ped.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <algorithm>
#include <cstring>
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

// aiCityData defaults when city/<map>.aimap has no [Traffic Lights].
constexpr const char* kDefaultSingleLight = "sp_traflitsingle_f";
constexpr const char* kDefaultDualLight = "sp_traflitdual_f";

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
        // Frame counts of the .anim files (u32 at offset 4) for clamping.
        for (const auto& st : info.table.states) {
            const std::string file = str::lower(st.animFile);
            if (info.animFrames.contains(file))
                continue;
            if (auto anim = vfs.readAll("anim/" + file + ".anim"); anim && anim->size() >= 8) {
                std::uint32_t frames = 0;
                std::memcpy(&frames, anim->data() + 4, 4);
                info.animFrames[file] = static_cast<int>(frames);
            }
        }
        if (auto shaders = vfs.readAll("anim/" + name + ".shaders"))
            if (auto set = asset::parsePedShaders(*shaders))
                info.variants = std::max<int>(1, static_cast<int>(set->variantCount));
        types.push_back(std::move(info));
    }
    return types;
}

std::optional<city::AiMapConfig> loadCityAiConfig(const vfs::Vfs& vfs, const city::CityData& city) {
    const std::string path = "city/" + str::lower(city.info.mapName) + ".aimap";
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    std::string error;
    auto config = city::parseAiMapConfig(asText(*bytes), &error);
    if (!config)
        log::warn("ai: {}: {}", path, error);
    return config;
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
    // aiMap::Init reads the city's own AI map (aiCityData: speed limit,
    // driving side, vehicle types, light models) and the race's (aiRaceData:
    // exceptions, lane changes, vehicle types).
    const std::optional<city::AiMapConfig> cityConfig = loadCityAiConfig(vfs, city);

    std::unique_ptr<World> world(new World());
    NetworkOptions net;
    if (cityConfig && cityConfig->driveOnLeft) {
        net.driveOnLeft = *cityConfig->driveOnLeft != 0;
    } else {
        // No city file: fall back to the cruise maps, which repeat the flag.
        auto dol = [](const std::optional<city::AiMapConfig>& c) {
            return c && c->driveOnLeft && *c->driveOnLeft != 0;
        };
        net.driveOnLeft = dol(city.cruise) || dol(city.cruisePro);
    }
    if (cityConfig && cityConfig->speedLimit)
        net.defaultSpeedLimit = *cityConfig->speedLimit;
    if (config)
        net.exceptions = config->exceptions;
    world->m_network = std::make_unique<RoadNetwork>(RoadNetwork::build(*city.aiMap, net));
    world->m_rooms = std::make_unique<city::RoomLocator>(city.psdl);
    world->m_lights.build(*world->m_network);

    TrafficSettings traffic;
    traffic.density = settings.trafficDensity;
    traffic.poolSize = settings.maxCars;
    traffic.laneChanges = !config || !config->ambientLaneChanges || *config->ambientLaneChanges != 0;
    // The race's [Ambient Types/Density] when it has one, else the city's.
    if (config && !config->ambientTypes.empty())
        traffic.types = config->ambientTypes;
    else if (cityConfig && !cityConfig->ambientTypes.empty())
        traffic.types = cityConfig->ambientTypes;
    else
        traffic.types = defaultAmbientTypes(vfs, city);
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

    // aiMap::Init: trunc([Ped Pool] x density) pedestrians of the race's
    // (else the city's) good- or bad-weather types.
    PedSettings peds;
    peds.density = settings.pedestrianDensity;
    peds.pool = settings.maxPeds >= 0 ? settings.maxPeds : kDefaultPedPool;
    if (settings.maxPeds < 0 && cityConfig) {
        for (const auto& sec : cityConfig->sections)
            if (str::iequals(sec.name, "Ped Pool") && !sec.lines.empty())
                peds.pool = static_cast<int>(str::parseDouble(sec.lines.front()).value_or(kDefaultPedPool));
    }
    std::vector<std::pair<std::string, std::string>> names;
    if (config && !config->pedNames.empty())
        names = config->pedNames;
    else if (cityConfig)
        names = cityConfig->pedNames;
    for (const auto& n : names)
        peds.names.push_back(settings.winterPeds ? n.second : n.first);
    world->m_peds =
        std::make_unique<Pedestrians>(*world->m_network, loadPedTypes(vfs), peds, settings.seed * 7919u + 1u);
    world->m_peds->setLights(&world->m_lights);
    Traffic* ambient = world->m_traffic.get();
    world->m_peds->setAccidentQuery(
        [ambient](int node, int path) { return ambient->accidentAt(node, path); });

    // Traffic light poles (aiTrafficLightSet::SetFourWay,
    // aiTrafficLightInstance::Init): the city's single-head model for
    // approaches with one lane, its second model for two or more.
    std::string single = kDefaultSingleLight, dual = kDefaultDualLight;
    if (cityConfig && cityConfig->trafficLights.size() >= 2) {
        single = str::lower(cityConfig->trafficLights[0]);
        dual = str::lower(cityConfig->trafficLights[1]);
    }
    for (const auto& site : world->m_network->lights()) {
        Signal s;
        s.model = site.arrivingLanes >= 2 ? dual : single;
        // X along the unit direction from the pole to trafficLightAxis
        // (away from the road on retail data), Z = (-x.z, 0, x.x): the glows
        // on +Z face the approaching traffic.
        const Vec3 x = site.axis;
        Mat34 m;
        m.m0 = x;
        m.m1 = Vec3::yAxis();
        m.m2 = {-x.z, 0.0f, x.x};
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

void World::step(const PlayerCar& player) {
    m_traffic->step(kAiStepSeconds, player, roomAt(player.transform.m3));
    m_peds->step(kAiStepSeconds, player, roomAt(player.transform.m3));
    m_lights.update(kAiStepSeconds);
    updateSignals();
}

void World::update(float dt, const PlayerCar& player) {
    m_accumulator += dt;
    int steps = 0;
    while (m_accumulator >= kAiStepSeconds && steps < 8) {
        step(player);
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
