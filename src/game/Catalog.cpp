#include "game/Catalog.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

namespace mm2::game {
namespace {

std::string_view asText(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

std::optional<VehicleInfo> parseVehicleInfo(std::string_view text) {
    const auto kv = data::KeyValueFile::parse(text);
    VehicleInfo v;
    v.baseName = str::lower(kv.getString("BaseName"));
    if (v.baseName.empty())
        return std::nullopt;
    v.description = kv.getString("Description", v.baseName);
    v.colors = kv.getList("Colors");
    v.flags = static_cast<std::uint32_t>(kv.getInt("Flags"));
    v.order = kv.getInt("Order", -1);
    v.scoringBias = kv.getFloat("ScoringBias");
    v.unlockScore = kv.getInt("UnlockScore");
    v.unlockFlags = static_cast<std::uint32_t>(kv.getInt("UnlockFlags"));
    v.horsepower = kv.getInt("Horsepower");
    v.topSpeedMph = kv.getInt("Top Speed");
    v.durability = kv.getInt("Durability");
    v.massLb = kv.getInt("Mass");
    v.uiDistance = kv.getFloat("UIDist");
    v.forceFeedbackModifier = kv.getFloat("ForceFeedbackModifier");
    v.roadForceModifier = kv.getFloat("RoadForceModifier");
    return v;
}

std::optional<CityInfo> parseCityInfo(std::string_view name, std::string_view text) {
    const auto kv = data::KeyValueFile::parse(text);
    CityInfo c;
    c.name = str::lower(name);
    c.localizedName = kv.getString("LocalizedName", name);
    c.mapName = str::lower(kv.getString("MapName", name));
    c.raceDir = str::lower(kv.getString("RaceDir", name));
    c.blitzNames = kv.getList("BlitzNames");
    c.circuitNames = kv.getList("CircuitNames");
    c.checkpointNames = kv.getList("CheckpointNames");
    // The counts are stored separately from the name lists; trust the counts.
    c.blitzNames.resize(static_cast<std::size_t>(kv.getInt("BlitzCount", static_cast<int>(c.blitzNames.size()))));
    c.circuitNames.resize(
        static_cast<std::size_t>(kv.getInt("CircuitCount", static_cast<int>(c.circuitNames.size()))));
    c.checkpointNames.resize(
        static_cast<std::size_t>(kv.getInt("CheckpointCount", static_cast<int>(c.checkpointNames.size()))));
    c.mustPlace = kv.getInt("MustPlace");
    c.unlockGroup = kv.getInt("UnlockGroup");
    return c;
}

Catalog Catalog::load(const vfs::Vfs& vfs) {
    Catalog cat;
    if (auto list = vfs.readAll("tune/cars.txt")) {
        for (auto line : data::splitLines(asText(*list))) {
            line = str::trim(line);
            if (line.empty())
                continue;
            const std::string path = "tune/" + str::lower(line);
            auto bytes = vfs.readAll(path);
            if (!bytes) {
                log::warn("catalog: {} listed in tune/cars.txt is missing", path);
                continue;
            }
            if (auto v = parseVehicleInfo(asText(*bytes)))
                cat.m_vehicles.push_back(std::move(*v));
            else
                log::warn("catalog: {} has no BaseName", path);
        }
    } else {
        log::warn("catalog: tune/cars.txt missing");
    }

    for (const auto& e : vfs.listFiles()) {
        if (!e.path.starts_with("tune/") || !e.path.ends_with(".cinfo") || e.path.find('/', 5) != std::string::npos)
            continue;
        const std::string stem = e.path.substr(5, e.path.size() - 5 - 6);
        if (auto bytes = vfs.readAll(e.path))
            if (auto c = parseCityInfo(stem, asText(*bytes)))
                cat.m_cities.push_back(std::move(*c));
    }
    log::info("catalog: {} vehicles, {} cities", cat.m_vehicles.size(), cat.m_cities.size());
    return cat;
}

const VehicleInfo* Catalog::vehicle(std::string_view baseName) const {
    for (const auto& v : m_vehicles)
        if (str::iequals(v.baseName, baseName))
            return &v;
    return nullptr;
}

const CityInfo* Catalog::city(std::string_view name) const {
    for (const auto& c : m_cities)
        if (str::iequals(c.name, name))
            return &c;
    return nullptr;
}

} // namespace mm2::game
