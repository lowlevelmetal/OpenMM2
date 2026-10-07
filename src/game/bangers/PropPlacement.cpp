#include "game/bangers/PropPlacement.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"
#include "game/fx/Random.h"

#include <cmath>
#include <format>
#include <map>

namespace mm2::game::bangers {
namespace {

std::string_view text(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

bool degenerate(const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; }

// A road's sidewalk edge (lvlAiMap's road vertices for one side): the curb
// and outer edge polylines through all the road's rooms, and the room of
// each segment.
struct Sidewalk {
    std::vector<Vec3> curb, outer;
    std::vector<int> room; // per point; a segment belongs to its first point's room
};

// lvlSDL::IsoLerp: the point `d` metres along a polyline (and its segment's
// room); false past the end.
bool isoLerp(const std::vector<Vec3>& line, const std::vector<int>& rooms, float d, Vec3& out, int& room) {
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const Vec3 a = line[i], b = line[i + 1];
        const float len = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
        room = rooms[i];
        if (d <= len) {
            const float t = d / len;
            out = {(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y, (b.z - a.z) * t + a.z};
            return true;
        }
        d -= len;
    }
    return false;
}

} // namespace

std::vector<PropDef> parsePropDefs(std::string_view t) {
    std::vector<PropDef> out;
    const auto csv = data::CsvTable::parse(t);
    auto col = [&](const char* name, int fallback) {
        const int c = csv.column(name);
        return c >= 0 ? static_cast<std::size_t>(c) : static_cast<std::size_t>(fallback);
    };
    const std::size_t start = col("start", 1), distance = col("distance", 2), maxUse = col("maxUse", 3),
                      minLerp = col("minLerp", 4), maxLerp = col("maxLerp", 5), file1 = col("file1", 6);
    for (std::size_t r = 0; r < csv.rows().size(); ++r) {
        const auto& row = csv.rows()[r];
        if (row.empty() || row[0].empty())
            continue;
        PropDef d;
        d.name = str::lower(row[0]);
        d.start = csv.cellFloat(r, start);
        d.distance = csv.cellFloat(r, distance, 1.0f);
        d.maxUse = csv.cellInt(r, maxUse, 1);
        d.minLerp = csv.cellFloat(r, minLerp, 0.1f);
        d.maxLerp = csv.cellFloat(r, maxLerp, d.minLerp);
        // file1..file4; the variant count stops at the first missing one.
        for (std::size_t c = file1; c < file1 + 4 && c < row.size() && !row[c].empty(); ++c)
            d.files.push_back(str::lower(row[c]));
        if (!d.files.empty())
            out.push_back(std::move(d));
    }
    return out;
}

std::vector<PropRule> parsePropRules(std::string_view t) {
    std::vector<PropRule> out;
    const auto csv = data::CsvTable::parse(t);
    for (const auto& row : csv.rows()) {
        if (row.empty() || row[0].empty())
            continue;
        PropRule r;
        r.name = str::lower(row[0]);
        // prop1... up to the end of the row; empty cells are skipped.
        for (std::size_t c = 1; c < row.size(); ++c)
            if (!row[c].empty())
                r.props.push_back(str::lower(row[c]));
        out.push_back(std::move(r));
    }
    return out;
}

PathPlacement decodePathPlacement(const city::PathSetPath& path) {
    PathPlacement p;
    if (path.points.empty())
        return p;
    // dgPath::Load: type byte, then the spacing byte in quarter metres
    // (fmul by 0.25); a spacing of 0 means 5 m.
    const std::uint32_t w = path.points.back().extra;
    p.type = static_cast<int>(w & 0xFF);
    const auto spacing = (w >> 8) & 0xFF;
    p.spacing = spacing ? static_cast<float>(spacing) * 0.25f : 5.0f;
    if (p.type > 2)
        p.type = 0;
    return p;
}

std::vector<PlacedProp> placePathSet(const city::PathSet& set, PlacedProp::Source source,
                                     const BangerDataLibrary* bangerOnly) {
    std::vector<PlacedProp> out;
    for (const auto& path : set.paths) {
        // Names may carry a prefix ("open:giz_bridge02_l").
        std::string model = str::lower(path.name);
        if (const auto colon = model.find(':'); colon != std::string::npos)
            model = model.substr(colon + 1);
        if (model.empty() || path.points.empty() || (bangerOnly && !bangerOnly->has(model)))
            continue;
        const PathPlacement pl = decodePathPlacement(path);
        const auto& pts = path.points;
        if (pl.type == 2) {
            // Each segment on its own: floor(length / spacing) props at equal
            // steps from its start, +X along the (possibly sloping) segment.
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const Vec3 a = pts[i].position, b = pts[i + 1].position;
                const float length = a.dist(b);
                const int n = static_cast<int>(std::floor(length / pl.spacing));
                if (n <= 0)
                    continue;
                const Vec3 x = (b - a) * (1.0f / length);
                const Vec3 z = x.cross({0, 1, 0});
                if (degenerate(z)) {
                    log::debug("bangers: {}: vertical path segment skipped", model);
                    continue;
                }
                Mat34 m;
                m.m0 = x;
                m.m2 = z.normalized();
                m.m1 = m.m2.cross(m.m0);
                const float step = length / static_cast<float>(n);
                for (int k = 0; k < n; ++k) {
                    m.m3 = a + x * (step * static_cast<float>(k));
                    out.push_back({model, m, 0, source, true});
                }
            }
        } else if (pl.type == 1) {
            // Position / direction pairs: +X towards the second point.
            for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
                Vec3 x = pts[i + 1].position - pts[i].position;
                x.y = 0.0f;
                if (degenerate(x))
                    continue;
                Mat34 m;
                m.m0 = x.normalized();
                m.m1 = {0, 1, 0};
                m.m2 = m.m0.cross(m.m1);
                m.m3 = pts[i].position;
                out.push_back({model, m, 0, source, false});
            }
        } else {
            for (const auto& p : pts)
                out.push_back({model, Mat34::translation(p.position), 0, source, false});
        }
    }
    return out;
}

std::vector<PlacedProp> placeStreetProps(const city::Psdl& psdl, const std::vector<PropDef>& defs,
                                         const std::vector<PropRule>& rules) {
    std::vector<PlacedProp> out;
    std::map<std::string, const PropDef*, std::less<>> defByName;
    for (const auto& d : defs)
        defByName[d.name] = &d;
    std::map<std::string, const PropRule*, std::less<>> ruleByName;
    for (const auto& r : rules)
        ruleByName[r.name] = &r;

    for (const auto& road : psdl.roads) {
        // Roads without sidewalks (flag 0x40 clear) have their curb on the
        // outer edge: nothing is placed.
        if (road.rooms.empty() || !(road.flags & 0x40))
            continue;
        const auto firstRoom = city::PsdlRoad::roomId(road.rooms.front());
        if (firstRoom >= psdl.rooms.size())
            continue;
        const int ruleNumber = psdl.rooms[firstRoom].propRule;
        const PropRule* sideRules[2] = {nullptr, nullptr};
        if (auto it = ruleByName.find(std::format("n{:02}left", ruleNumber)); it != ruleByName.end())
            sideRules[0] = it->second;
        if (auto it = ruleByName.find(std::format("n{:02}right", ruleNumber)); it != ruleByName.end())
            sideRules[1] = it->second;
        if (!sideRules[0] && !sideRules[1])
            continue;

        // The road's sections through all its rooms (lvlAiMap::SetRoad); a
        // room whose first section repeats the previous room's last one
        // continues it. Joining and orientation are inferred: rooms whose
        // sections run backwards are reversed.
        struct Section {
            std::uint16_t v[6];
            int stride;
            int room;
        };
        std::vector<Section> sections;
        for (const auto rid : road.rooms) {
            const auto r = city::PsdlRoad::roomId(rid);
            if (r >= psdl.rooms.size())
                continue;
            std::vector<Section> mine;
            for (const auto& a : psdl.rooms[r].attributes) {
                const int stride = a.type == city::PsdlAttrType::RoadStrip           ? 4
                                   : a.type == city::PsdlAttrType::DividedRoadStrip ? 6
                                                                                     : 0;
                if (!stride)
                    continue;
                const auto v = a.vertices();
                for (std::size_t s = 0; s + static_cast<std::size_t>(stride) <= v.size(); s += static_cast<std::size_t>(stride)) {
                    Section sec{};
                    sec.stride = stride;
                    sec.room = static_cast<int>(r);
                    for (int k = 0; k < stride; ++k)
                        sec.v[k] = v[s + static_cast<std::size_t>(k)];
                    mine.push_back(sec);
                }
            }
            if (mine.empty())
                continue;
            auto same = [](const Section& a, const Section& b) {
                return a.stride == b.stride && std::equal(a.v, a.v + a.stride, b.v);
            };
            if (!sections.empty() && same(mine.back(), sections.back()))
                std::reverse(mine.begin(), mine.end());
            for (const auto& sec : mine)
                if (sections.empty() || !same(sec, sections.back()))
                    sections.push_back(sec);
        }
        if (sections.size() < 2)
            continue;
        auto vertex = [&](std::uint16_t i) { return i < psdl.vertices.size() ? psdl.vertices[i] : Vec3{}; };
        Sidewalk walks[2];
        for (const auto& sec : sections) {
            // Left: curb 1, outer 0. Right: curb 2 / outer 3 (road strips) or
            // curb 4 / outer 5 (divided roads).
            const int rc = sec.stride == 6 ? 4 : 2, ro = sec.stride == 6 ? 5 : 3;
            walks[0].curb.push_back(vertex(sec.v[1]));
            walks[0].outer.push_back(vertex(sec.v[0]));
            walks[1].curb.push_back(vertex(sec.v[rc]));
            walks[1].outer.push_back(vertex(sec.v[ro]));
            walks[0].room.push_back(sec.room);
            walks[1].room.push_back(sec.room);
        }
        // Walks per prop: one per road strip of the first room.
        int strips = 0;
        for (const auto& a : psdl.rooms[firstRoom].attributes)
            if (a.type == city::PsdlAttrType::RoadStrip || a.type == city::PsdlAttrType::DividedRoadStrip)
                ++strips;

        fx::Rand rng(1); // ResetRandomSeed
        for (int ruleSide = 0; ruleSide < 2; ++ruleSide) {
            if (!sideRules[ruleSide])
                continue;
            for (const auto& propName : sideRules[ruleSide]->props) {
                const auto defIt = defByName.find(propName);
                if (defIt == defByName.end())
                    continue;
                const PropDef& d = *defIt->second;
                int maxUse = d.maxUse;
                for (int strip = 0; strip < strips; ++strip) {
                    for (int side = 0; side < 2; ++side) {
                        const Sidewalk& w = walks[side];
                        float along = d.start;
                        Vec3 curb, outer;
                        int curbRoom = 0, outerRoom = 0;
                        while (isoLerp(w.curb, w.room, along, curb, curbRoom) &&
                               isoLerp(w.outer, w.room, along, outer, outerRoom)) {
                            const float f = (d.maxLerp - d.minLerp) * rng.frand() + d.minLerp;
                            Mat34 m;
                            m.m0 = outer - curb;
                            m.m1 = {0, 1, 0};
                            m.m3 = {(outer.x - curb.x) * f + curb.x, (outer.y - curb.y) * f + curb.y + 0.15f,
                                    (outer.z - curb.z) * f + curb.z};
                            const float len2 = m.m0.mag2();
                            m.m0 = len2 == 0.0f ? Vec3{} : m.m0 * (1.0f / std::sqrt(len2));
                            m.m2 = m.m0.cross(m.m1);
                            if (!degenerate(m.m0) && !degenerate(m.m2) && side == ruleSide && maxUse != 0) {
                                --maxUse;
                                const std::string& file =
                                    d.files[static_cast<std::size_t>(rng.irand()) % d.files.size()];
                                out.push_back({file, m, outerRoom, PlacedProp::Source::StreetRule, false});
                            }
                            along = (d.distance - d.distance) * rng.frand() + d.distance + along;
                        }
                    }
                }
            }
        }
    }
    return out;
}

std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs, const BangerDataLibrary& data) {
    std::vector<PlacedProp> out;
    // .inst banger entries keep their matrix unless instance flag 0x80 asks
    // for a Y rotation (cityLevel::LoadInstances).
    for (const auto* list : {&city.instances, &city.aiInstances})
        for (const auto& inst : *list)
            if (data.has(inst.name))
                out.push_back({str::lower(inst.name), inst.transform, inst.room, PlacedProp::Source::Instance,
                               !(inst.flags & 0x80)});
    const std::string dir = "city/" + str::lower(city.info.mapName) + "/";
    const auto defs = vfs.readAll(dir + "propdefs.csv");
    const auto rules = vfs.readAll(dir + "proprules.csv");
    if (defs && rules) {
        auto props = placeStreetProps(city.psdl, parsePropDefs(text(*defs)), parsePropRules(text(*rules)));
        out.insert(out.end(), props.begin(), props.end());
    }
    if (auto bytes = vfs.readAll(dir + "props.pathset")) {
        std::string error;
        if (auto set = city::parsePathSet(*bytes, &error)) {
            auto props = placePathSet(*set, PlacedProp::Source::PathSet, &data);
            out.insert(out.end(), props.begin(), props.end());
        } else {
            log::warn("bangers: {}props.pathset: {}", dir, error);
        }
    }
    return out;
}

} // namespace mm2::game::bangers
