#include "game/Catalog.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <format>

namespace mm2::game {
namespace {

std::string_view asText(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Whether every key is present (MM2 reads them with fscanf and gives up at
// the first one missing).
bool hasAll(const data::KeyValueFile& kv, std::initializer_list<std::string_view> keys) {
    for (const auto key : keys)
        if (!kv.get(key))
            return false;
    return true;
}

// mmCityInfo::Load: a non-zero count becomes the number of names in the
// list (string::NumSubStrings); a zero count ignores the list.
std::vector<std::string> namesFor(const data::KeyValueFile& kv, std::string_view countKey,
                                  std::string_view listKey) {
    if (kv.getInt(countKey) == 0)
        return {};
    return kv.getList(listKey);
}

// tune/<stem><extension> directly in tune/, with a non-empty stem.
bool isTuneFile(const std::string& lowerPath, std::string_view extension) {
    return lowerPath.starts_with("tune/") && lowerPath.ends_with(extension) &&
           lowerPath.find('/', 5) == std::string::npos && lowerPath.size() > 5 + extension.size();
}

} // namespace

std::optional<VehicleInfo> parseVehicleInfo(std::string_view text) {
    // mmVehInfo::Load
    const auto kv = data::KeyValueFile::parse(text);
    if (!hasAll(kv, {"BaseName", "Description", "Colors", "Flags", "Order", "ScoringBias", "UnlockScore",
                     "UnlockFlags", "Horsepower", "Top Speed", "Durability", "Mass"}))
        return std::nullopt;
    VehicleInfo v;
    v.baseName = str::lower(kv.getString("BaseName"));
    if (v.baseName.empty())
        return std::nullopt;
    v.description = kv.getString("Description");
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
    v.uiDistance = kv.getFloat("UIDist", 6.0f);
    return v;
}

std::optional<CityInfo> parseCityInfo(std::string_view name, std::string_view text) {
    // mmCityInfo::Load
    const auto kv = data::KeyValueFile::parse(text);
    if (!hasAll(kv, {"LocalizedName", "MapName", "RaceDir", "BlitzCount", "CircuitCount", "CheckpointCount",
                     "BlitzNames", "CircuitNames", "CheckpointNames"}))
        return std::nullopt;
    CityInfo c;
    c.name = str::lower(name);
    c.localizedName = kv.getString("LocalizedName");
    c.mapName = str::lower(kv.getString("MapName"));
    c.raceDir = str::lower(kv.getString("RaceDir"));
    c.blitzNames = namesFor(kv, "BlitzCount", "BlitzNames");
    c.circuitNames = namesFor(kv, "CircuitCount", "CircuitNames");
    c.checkpointNames = namesFor(kv, "CheckpointCount", "CheckpointNames");
    return c;
}

Catalog Catalog::load(const vfs::Vfs& vfs) {
    Catalog cat;
    // MM2 mmVehList::LoadAll: the cars of its built-in list in that order,
    // then every tune/*.info the directory enumeration finds (LoadVehListCB,
    // in the order of the game data). mmVehList::Load drops a car whose
    // BaseName is already listed. tune/cars.txt is never read.
    static constexpr const char* kBuiltIn[] = {"vpcoop",   "vpbug",    "vpcab",   "vpcaddie", "vpford",
                                               "vpmustang99", "vpcop", "vpbullet", "vppanoz", "vpbus",
                                               "vpddbus",  "vpcentury", "vpcoop2k", "vpdune",  "vpvwcup",
                                               "vp4x4",    "vpauditt", "vpdb7",   "vppanozgt", "vpsemi"};
    auto loadInfo = [&](const std::string& path) {
        auto bytes = vfs.readAll(path);
        if (!bytes)
            return;
        auto v = parseVehicleInfo(asText(*bytes));
        if (!v) {
            log::warn("catalog: {} is not a complete vehicle description", path);
            return;
        }
        if (!cat.vehicle(v->baseName))
            cat.m_vehicles.push_back(std::move(*v));
    };
    for (const char* name : kBuiltIn)
        loadInfo(std::format("tune/{}.info", name));
    for (const auto& e : vfs.listFiles())
        if (isTuneFile(str::lower(e.path), ".info"))
            loadInfo(e.path);

    // MM2 mmCityList::LoadAll: sf.cinfo first, then every tune/*.cinfo;
    // mmCityList::Load drops a city whose RaceDir is already listed.
    auto loadCity = [&](const std::string& path) {
        const std::string lower = str::lower(path);
        auto bytes = vfs.readAll(path);
        if (!bytes)
            return;
        auto c = parseCityInfo(lower.substr(5, lower.size() - 5 - 6), asText(*bytes));
        if (!c) {
            log::warn("catalog: {} is not a complete city description", path);
            return;
        }
        if (!cat.city(c->raceDir))
            cat.m_cities.push_back(std::move(*c));
    };
    loadCity("tune/sf.cinfo");
    for (const auto& e : vfs.listFiles())
        if (isTuneFile(str::lower(e.path), ".cinfo"))
            loadCity(e.path);
    log::info("catalog: {} vehicles, {} cities", cat.m_vehicles.size(), cat.m_cities.size());
    return cat;
}

const VehicleInfo* Catalog::vehicle(std::string_view baseName) const {
    for (const auto& v : m_vehicles)
        if (str::iequals(v.baseName, baseName))
            return &v;
    return nullptr;
}

const CityInfo* Catalog::city(std::string_view raceDir) const {
    for (const auto& c : m_cities)
        if (str::iequals(c.raceDir, raceDir))
            return &c;
    return nullptr;
}

} // namespace mm2::game
