#include "game/bangers/PropPlacement.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <cmath>
#include <format>
#include <map>

namespace mm2::game::bangers {
namespace {

// Rotation about +Y so that the model's +X axis points along `x` (projected
// onto the ground plane).
Mat34 yawFrame(const Vec3& xAxis, const Vec3& pos) {
    Vec3 x{xAxis.x, 0.0f, xAxis.z};
    x = x.mag2() > 1e-8f ? x.normalized() : Vec3{1, 0, 0};
    Mat34 m;
    m.m0 = x;
    m.m1 = {0, 1, 0};
    m.m2 = x.cross(m.m1);
    m.m3 = pos;
    return m;
}

std::string_view text(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

// Walks a polyline and calls `fn(position, tangent, index)` every `spacing`
// metres starting at `start`, at most `max` times.
template <class Fn>
void walk(const std::vector<Vec3>& line, float start, float spacing, int max, Fn&& fn) {
    if (line.size() < 2 || max <= 0)
        return;
    spacing = std::max(spacing, 0.1f);
    float target = start, travelled = 0.0f;
    int placed = 0;
    for (std::size_t i = 1; i < line.size() && placed < max; ++i) {
        const Vec3 a = line[i - 1], b = line[i];
        const float len = a.dist(b);
        if (len < 1e-4f)
            continue;
        while (target <= travelled + len && placed < max) {
            const float t = (target - travelled) / len;
            fn(lerp(a, b, t), (b - a) * (1.0f / len), placed);
            ++placed;
            target += spacing;
        }
        travelled += len;
    }
}

} // namespace

std::vector<PropDef> parsePropDefs(std::string_view t) {
    std::vector<PropDef> out;
    const auto csv = data::CsvTable::parse(t);
    for (std::size_t r = 0; r < csv.rows().size(); ++r) {
        const auto& row = csv.rows()[r];
        if (row.empty() || row[0].empty())
            continue;
        PropDef d;
        d.name = str::lower(row[0]);
        d.start = csv.cellFloat(r, 1);
        d.distance = csv.cellFloat(r, 2, 1.0f);
        d.maxUse = csv.cellInt(r, 3, 1);
        d.minLerp = csv.cellFloat(r, 4, 0.1f);
        d.maxLerp = csv.cellFloat(r, 5, d.minLerp);
        for (std::size_t c = 6; c < row.size(); ++c)
            if (!row[c].empty())
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
    const std::uint32_t w = path.points.back().extra;
    p.type = static_cast<int>(w & 0xFF);
    p.spacing = static_cast<float>((w >> 8) & 0xFF) * 0.1f;
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
        auto add = [&](const Vec3& pos, const Vec3& xAxis) {
            out.push_back({model, yawFrame(xAxis, pos), 0, source});
        };
        if (pl.type == 2 && path.points.size() >= 2) {
            std::vector<Vec3> line;
            for (const auto& p : path.points)
                line.push_back(p.position);
            walk(line, 0.0f, pl.spacing > 0.0f ? pl.spacing : 2.0f, 4096,
                 [&](const Vec3& pos, const Vec3& dir, int) { add(pos, dir); });
        } else if (pl.type == 1 && path.points.size() >= 2) {
            // Position / look-at pairs; the model faces the second point (-Z towards it).
            for (std::size_t i = 0; i + 1 < path.points.size(); i += 2) {
                const Vec3 pos = path.points[i].position, at = path.points[i + 1].position;
                const Vec3 fwd = at - pos;
                add(pos, Vec3{-fwd.z, 0, fwd.x}); // +X to the right of the facing direction
            }
        } else {
            for (const auto& p : path.points)
                add(p.position, {1, 0, 0});
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

    std::size_t variant = 0;
    for (std::size_t room = 1; room < psdl.rooms.size(); ++room) {
        const auto& rm = psdl.rooms[room];
        if (rm.propRule == 0)
            continue;
        const auto left = ruleByName.find(std::format("n{:02}left", rm.propRule));
        const auto right = ruleByName.find(std::format("n{:02}right", rm.propRule));
        if (left == ruleByName.end() && right == ruleByName.end())
            continue;
        for (const auto& a : rm.attributes) {
            if (a.type != city::PsdlAttrType::RoadStrip)
                continue;
            const auto v = a.vertices();
            if (v.size() < 8 || v.size() % 4 != 0)
                continue;
            // Per section: outer L, curb L, curb R, outer R.
            for (int side = 0; side < 2; ++side) {
                const auto& ruleIt = side == 0 ? left : right;
                if (ruleIt == ruleByName.end())
                    continue;
                std::vector<Vec3> curb, outer;
                for (std::size_t s = 0; s + 3 < v.size(); s += 4) {
                    const auto c = side == 0 ? v[s + 1] : v[s + 2];
                    const auto o = side == 0 ? v[s + 0] : v[s + 3];
                    if (c >= psdl.vertices.size() || o >= psdl.vertices.size())
                        continue;
                    curb.push_back(psdl.vertices[c]);
                    outer.push_back(psdl.vertices[o]);
                }
                if (curb.size() < 2)
                    continue;
                for (const auto& propName : ruleIt->second->props) {
                    const auto defIt = defByName.find(propName);
                    if (defIt == defByName.end())
                        continue;
                    const PropDef& d = *defIt->second;
                    // Walk the curb; find the matching outer point by segment fraction.
                    walk(curb, d.start, d.distance, d.maxUse, [&](const Vec3& pos, const Vec3&, int) {
                        // Nearest outer point: interpolate on the same segment index.
                        std::size_t seg = 0;
                        float best = 1e30f, bestT = 0.0f;
                        for (std::size_t i = 1; i < curb.size(); ++i) {
                            const Vec3 ab = curb[i] - curb[i - 1];
                            const float len2 = ab.mag2();
                            const float t = len2 > 0 ? clampf((pos - curb[i - 1]).dot(ab) / len2, 0, 1) : 0.0f;
                            const float d2 = (curb[i - 1] + ab * t).dist2(pos);
                            if (d2 < best) {
                                best = d2;
                                seg = i;
                                bestT = t;
                            }
                        }
                        const Vec3 o = lerp(outer[seg - 1], outer[seg], bestT);
                        // Sidewalks are raised to the outer edge's height.
                        Vec3 p = lerp(pos, o, d.minLerp);
                        p.y = o.y;
                        const Vec3 away = o - pos;
                        const std::string& file = d.files[variant++ % d.files.size()];
                        out.push_back({file, yawFrame(away, p), static_cast<int>(room), PlacedProp::Source::StreetRule});
                    });
                }
            }
        }
    }
    return out;
}

std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs, const BangerDataLibrary& data) {
    std::vector<PlacedProp> out;
    for (const auto* list : {&city.instances, &city.aiInstances})
        for (const auto& inst : *list)
            if (data.has(inst.name))
                out.push_back({str::lower(inst.name), inst.transform, inst.room, PlacedProp::Source::Instance});
    const std::string dir = "city/" + str::lower(city.info.mapName) + "/";
    if (auto bytes = vfs.readAll(dir + "props.pathset")) {
        std::string error;
        if (auto set = city::parsePathSet(*bytes, &error)) {
            auto props = placePathSet(*set, PlacedProp::Source::PathSet, &data);
            out.insert(out.end(), props.begin(), props.end());
        } else {
            log::warn("bangers: {}props.pathset: {}", dir, error);
        }
    }
    const auto defs = vfs.readAll(dir + "propdefs.csv");
    const auto rules = vfs.readAll(dir + "proprules.csv");
    if (defs && rules) {
        auto props = placeStreetProps(city.psdl, parsePropDefs(text(*defs)), parsePropRules(text(*rules)));
        out.insert(out.end(), props.begin(), props.end());
    }
    return out;
}

} // namespace mm2::game::bangers
