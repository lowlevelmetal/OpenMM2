#include "city/AiMap.h"

#include "city/Reader.h"

#include <format>

namespace mm2::city {
namespace {

bool readSide(detail::Reader& r, std::size_t sections, AiRoadSide& s) {
    s.numLanes = r.u16();
    s.numTrams = r.u16();
    s.numTrains = r.u16();
    s.numSidewalks = r.u16();
    s.roadType = r.u16();
    s.unknown5 = r.u16();
    s.unknown6 = r.u16();
    if (!r.ok() || s.numLanes > 64 || s.numTrams > 64 || s.numTrains > 64)
        return false;
    const std::size_t entities = s.numLanes + 1u;
    const std::size_t lengths = sections - 1;
    if ((entities * (lengths + 1) + s.numLanes + 10) * 4 > r.remaining())
        return false;
    s.laneLengths.assign(entities, {});
    s.laneEndValues.clear();
    for (auto& arr : s.laneLengths) {
        arr.resize(lengths);
        for (auto& v : arr)
            v = r.f32();
        s.laneEndValues.push_back(r.f32());
    }
    s.laneExtras.resize(s.numLanes);
    for (auto& v : s.laneExtras)
        v = r.f32();
    for (auto& v : s.params)
        v = r.f32();
    const std::size_t numPolylines = 3u + s.numLanes + s.numTrams + s.numTrains;
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
    for (std::size_t k = 0; k < numPaths; ++k) {
        auto& p = map.paths[k];
        p.id = r.u16();
        const std::uint16_t sections = r.u16();
        p.flags = r.u16();
        const std::uint16_t numRooms = r.u16();
        if (!r.ok() || sections < 2 || numRooms > r.remaining() / 2)
            return fail(std::format("path {}: bad header", k));
        p.rooms.resize(numRooms);
        for (auto& room : p.rooms)
            room = r.u16();
        p.halfWidth = r.f32();
        p.speedLimit = r.f32();
        if (!readSide(r, sections, p.left) || !readSide(r, sections, p.right))
            return fail(std::format("path {}: truncated lane data", k));
        p.unknown = r.u32();
        p.centerLengths.resize(sections - 1u);
        for (auto& v : p.centerLengths)
            v = r.f32();
        if (static_cast<std::size_t>(sections) * 60 > r.remaining())
            return fail(std::format("path {}: truncated section frames", k));
        for (auto* arr : {&p.center, &p.xAxis, &p.yAxis, &p.zAxis, &p.wAxis}) {
            arr->resize(sections);
            for (auto& v : *arr)
                v = r.vec3();
        }
        readEnd(r, p.ends[0]);
        readEnd(r, p.ends[1]);
        if (!r.ok())
            return fail(std::format("path {}: truncated", k));
    }

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
