#include "city/AiMap.h"

#include "city/Reader.h"
#include "city/SdlDraw.h"
#include "core/Libm.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::city {
namespace {

bool readSide(detail::Reader& r, std::size_t sections, AiRoadSide& s) {
    s.numLanes = r.u16();
    s.numTrams = r.u16();
    s.numTrains = r.u16();
    s.numSidewalks = r.u16();
    s.roadType = r.u16();
    if (!r.ok() || s.numLanes > 64 || s.numTrams > 64 || s.numTrains > 64 || s.numSidewalks > 64)
        return false;
    // One row of lengths per lane and sidewalk. (OpenMM2 reads the first
    // length as two u16, unknown5 and unknown6; a side with no rows, as the
    // shortcut roads' first side, has none.)
    const std::size_t entities = static_cast<std::size_t>(s.numLanes) + s.numSidewalks;
    s.unknown5 = s.unknown6 = 0;
    s.laneLengths.clear();
    s.laneEndValues.clear();
    s.laneExtras.clear();
    if (entities > 0) {
        s.unknown5 = r.u16();
        s.unknown6 = r.u16();
        const std::size_t lengths = sections - 1;
        if ((entities * (lengths + 1) + (entities - 1) + 10) * 4 > r.remaining())
            return false;
        s.laneLengths.assign(entities, {});
        for (auto& arr : s.laneLengths) {
            arr.resize(lengths);
            for (auto& v : arr)
                v = r.f32();
            s.laneEndValues.push_back(r.f32());
        }
        s.laneExtras.resize(entities - 1);
        for (auto& v : s.laneExtras)
            v = r.f32();
    }
    for (auto& v : s.params)
        v = r.f32();
    const std::size_t numPolylines = entities + s.numTrams + s.numTrains + 2u;
    if (numPolylines * sections * 12 > r.remaining())
        return false;
    s.polylines.assign(numPolylines, {});
    for (auto& line : s.polylines) {
        line.resize(sections);
        for (auto& p : line)
            p = r.vec3();
    }
    return r.ok();
}

void readEnd(detail::Reader& r, AiPathEnd& e) {
    e.intersection = r.u32();
    e.unknown1 = r.u16();
    e.vehicleRule = r.u16();
    e.unknown2 = r.u16();
    e.roadIndex = r.u16();
    e.unknown3 = r.u16();
    e.trafficLightPos = r.vec3();
    e.trafficLightAxis = r.vec3();
}

} // namespace

// One path record (aiPath::ReadBinary). Returns an error message, empty on
// success.
std::string readPath(detail::Reader& r, std::size_t k, AiPath& p) {
    p.id = r.u16();
    const std::uint16_t sections = r.u16();
    p.flags = r.u16();
    const std::uint16_t numRooms = r.u16();
    if (!r.ok() || sections < 2 || numRooms > r.remaining() / 2)
        return std::format("path {}: bad header", k);
    p.rooms.resize(numRooms);
    for (auto& room : p.rooms)
        room = r.u16();
    p.halfWidth = r.f32();
    p.speedLimit = r.f32();
    if (!readSide(r, sections, p.left) || !readSide(r, sections, p.right))
        return std::format("path {}: truncated lane data", k);
    p.unknown = r.u32();
    p.centerLengths.resize(sections - 1u);
    for (auto& v : p.centerLengths)
        v = r.f32();
    if (static_cast<std::size_t>(sections) * 60 > r.remaining())
        return std::format("path {}: truncated section frames", k);
    for (auto* arr : {&p.center, &p.xAxis, &p.yAxis, &p.zAxis, &p.wAxis}) {
        arr->resize(sections);
        for (auto& v : *arr)
            v = r.vec3();
    }
    readEnd(r, p.ends[0]);
    readEnd(r, p.ends[1]);
    if (!r.ok())
        return std::format("path {}: truncated", k);
    return {};
}

std::optional<AiMap> parseBai(std::span<const std::byte> data, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<AiMap> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    detail::Reader r(data);
    if (!r.magic("CAI1"))
        return fail("not a BAI file (missing CAI1)");
    const std::uint16_t numIntersections = r.u16();
    const std::uint16_t numPaths = r.u16();

    AiMap map;
    map.paths.resize(numPaths);
    for (std::size_t k = 0; k < numPaths; ++k)
        if (std::string err = readPath(r, k, map.paths[k]); !err.empty())
            return fail(std::move(err));

    map.intersections.resize(numIntersections);
    for (std::size_t k = 0; k < numIntersections; ++k) {
        auto& in = map.intersections[k];
        in.id = r.u16();
        in.room = r.u16();
        in.center = r.vec3();
        const std::uint16_t n = r.u16();
        if (!r.ok() || n > r.remaining() / 4)
            return fail(std::format("intersection {}: truncated", k));
        in.paths.resize(n);
        for (auto& id : in.paths)
            id = r.u32();
    }

    const std::uint32_t numRooms = r.u32();
    if (!r.ok() || numRooms > r.remaining() / 2)
        return fail("bad room table");
    for (auto* table : {&map.roomPathsNear, &map.roomPathsIn}) {
        table->resize(numRooms);
        for (auto& list : *table) {
            const std::uint16_t n = r.u16();
            if (!r.ok() || n > r.remaining() / 2)
                return fail("truncated room table");
            list.resize(n);
            for (auto& id : list)
                id = r.u16();
        }
    }
    if (!r.atEnd())
        return fail(std::format("{} unexpected trailing bytes", r.remaining()));
    return map;
}

std::optional<std::vector<AiPath>> parseShortcutBai(std::span<const std::byte> data, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<std::vector<AiPath>> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    detail::Reader r(data);
    if (!r.magic("CAI1"))
        return fail("not a BAI file (missing CAI1)");
    const std::uint16_t count = r.u16();
    if (!r.ok())
        return fail("truncated header");
    std::vector<AiPath> paths(count);
    for (std::size_t k = 0; k < count; ++k)
        if (std::string err = readPath(r, k, paths[k]); !err.empty())
            return fail(std::move(err));
    if (!r.atEnd())
        return fail(std::format("{} unexpected trailing bytes", r.remaining()));
    return paths;
}

void addShortcuts(AiMap& map, std::vector<AiPath> shortcuts, const Psdl* psdl) {
    // aiIntersection::CreateRoadMap: the centre moved to the bound-sphere
    // centre of the intersection's room, then the list sorted by atan2(dx,
    // dz) of each road's far-from-centre end (its last centre vertex when the
    // road's end 0 is here, else its first), smallest first (a selection
    // sort, swapping on strictly smaller keys); then every listed road's
    // index at that end (its first place in the list; end 0 when that is
    // here).
    auto createRoadMap = [&](std::uint32_t node) {
        if (node >= map.intersections.size())
            return;
        AiIntersection& in = map.intersections[node];
        if (psdl) {
            float radius = 0.0f;
            sdlRoomBoundSphere(*psdl, in.room, in.center, radius);
        }
        const std::size_t n = in.paths.size();
        std::vector<float> key(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            if (in.paths[i] >= map.paths.size())
                continue;
            const AiPath& p = map.paths[in.paths[i]];
            if (p.center.empty())
                continue;
            const Vec3& at = p.ends[0].intersection == node ? p.center.back() : p.center.front();
            key[i] = libm::atan2(at.x - in.center.x, at.z - in.center.z);
        }
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                if (key[j] < key[i]) {
                    std::swap(key[i], key[j]);
                    std::swap(in.paths[i], in.paths[j]);
                }
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (in.paths[i] >= map.paths.size())
                continue;
            AiPath& p = map.paths[in.paths[i]];
            const auto first = static_cast<std::uint16_t>(
                std::find(in.paths.begin(), in.paths.end(), in.paths[i]) - in.paths.begin());
            if (p.ends[0].intersection == node)
                p.ends[0].roadIndex = first;
            else
                p.ends[1].roadIndex = first;
        }
    };
    const std::size_t first = map.paths.size();
    map.numShortcuts += shortcuts.size();
    for (std::size_t k = 0; k < shortcuts.size(); ++k) {
        AiPath p = std::move(shortcuts[k]);
        const std::size_t id = first + k;
        p.id = static_cast<std::uint16_t>(id);
        p.left.roadType = 3;
        p.right.roadType = 3;
        const std::uint32_t end1 = p.ends[1].intersection, end0 = p.ends[0].intersection;
        map.paths.push_back(std::move(p));
        for (const std::uint32_t node : {end1, end0}) {
            if (node < map.intersections.size())
                map.intersections[node].paths.push_back(static_cast<std::uint32_t>(id));
            createRoadMap(node);
        }
    }
}

std::vector<std::string> validateAiMap(const AiMap& map, std::size_t roomCount) {
    std::vector<std::string> problems;
    auto report = [&](std::string s) {
        if (problems.size() < 64)
            problems.push_back(std::move(s));
    };
    const std::size_t np = map.paths.size(), ni = map.intersections.size();
    for (std::size_t k = 0; k < np; ++k) {
        const auto& p = map.paths[k];
        if (p.id != k)
            report(std::format("path {} has id {}", k, p.id));
        for (auto room : p.rooms)
            if (room >= roomCount)
                report(std::format("path {}: room {} out of range", k, room));
        for (const auto& e : p.ends)
            if (e.intersection >= ni)
                report(std::format("path {}: intersection {} out of range", k, e.intersection));
    }
    for (std::size_t k = 0; k < ni; ++k) {
        const auto& in = map.intersections[k];
        if (in.id != k)
            report(std::format("intersection {} has id {}", k, in.id));
        if (in.room >= roomCount)
            report(std::format("intersection {}: room {} out of range", k, in.room));
        for (auto id : in.paths)
            if (id >= np)
                report(std::format("intersection {}: path {} out of range", k, id));
    }
    if (map.roomPathsNear.size() != roomCount)
        report(std::format("room table has {} rooms, PSDL has {}", map.roomPathsNear.size(), roomCount));
    for (const auto* table : {&map.roomPathsNear, &map.roomPathsIn})
        for (const auto& list : *table)
            for (auto id : list)
                if (id >= np)
                    report(std::format("room table: path {} out of range", id));
    return problems;
}

} // namespace mm2::city
