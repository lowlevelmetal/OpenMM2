// Where props come from: cityLevel::Load / LoadPathSet / LoadPath / LoadProp,
// dgPath::Load / Enumerate, cityPropulator, lvlSDL::Propulate / IsoLerp and
// lvlAiMap's sidewalk vertices, from the code of midtown2.exe build 3393
// (MM2Recomp). See docs/bangers.md.
// Also: lvlAiMap::GetNumRoads, lvlAiMap::GetNumRooms, lvlAiMap::GetNumVertexs,
// lvlAiMap::GetRoomChop and lvlAiMap::Delete (cityLevel::Load's road table).
#include "game/bangers/PropPlacement.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/CamMath.h"
#include "game/fx/Random.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cmath>
#include <format>
#include <map>
#include <span>

namespace mm2::game::bangers {
namespace {

std::string_view text(const std::vector<std::byte>& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

bool zeroRow(const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; }

// parCsvFile::Load, as cityPropulator::Load reads propdefs.csv and
// proprules.csv. The first line names the columns, at most 16 of them; every
// following line is a row, blank lines included, with at most one cell per
// column. Lines are read as fgets does into 256 bytes: one row per line, or
// per 255 characters of a longer line. A '#' ends the line. A cell starts
// after any control characters and runs to the next control character or
// comma (bytes from 0x80 count as control characters: MSVC's char is
// signed), which it consumes: an empty cell between two commas exists, but
// none follows a comma that ends the line. Cells keep their spaces. A row
// shorter than the header has no cells (MM2's null) after its last one.
class ParCsv {
public:
    static constexpr std::size_t kMaxColumns = 16;

    explicit ParCsv(std::string_view text) {
        bool header = true;
        std::size_t pos = 0;
        while (pos < text.size()) {
            // fgets(256): up to and including the newline, at most 255 bytes.
            std::size_t end = pos;
            while (end < text.size() && end - pos < 255 && text[end] != '\n')
                ++end;
            if (end < text.size() && end - pos < 255)
                ++end; // the newline
            std::string_view line = text.substr(pos, end - pos);
            pos = end;
            if (const auto hash = line.find('#'); hash != std::string_view::npos)
                line = line.substr(0, hash);
            if (header) {
                m_header = cells(line, kMaxColumns);
                header = false;
            } else {
                m_rows.push_back(cells(line, m_header.size()));
            }
        }
    }

    // parCsvFile::GetColumn: case-insensitive; -1 when missing (MM2 quits).
    int column(std::string_view name) const {
        for (std::size_t i = 0; i < m_header.size(); ++i)
            if (str::iequals(m_header[i], name))
                return static_cast<int>(i);
        return -1;
    }
    const std::vector<std::vector<std::string>>& rows() const { return m_rows; }

    // The cell, or nullptr where MM2 has none.
    static const std::string* cell(const std::vector<std::string>& row, int column) {
        return column >= 0 && static_cast<std::size_t>(column) < row.size() ? &row[static_cast<std::size_t>(column)]
                                                                             : nullptr;
    }

private:
    static bool control(char c) { return static_cast<signed char>(c) < 0x20; }

    static std::vector<std::string> cells(std::string_view line, std::size_t limit) {
        std::vector<std::string> out;
        std::size_t p = 0;
        while (p < line.size() && out.size() < limit) {
            while (p < line.size() && control(line[p]))
                ++p;
            const std::size_t start = p;
            while (p < line.size() && !control(line[p]) && line[p] != ',')
                ++p;
            const std::size_t stop = p;
            if (p < line.size())
                ++p; // the comma or control character ends the cell
            if (p != start)
                out.emplace_back(line.substr(start, stop - start));
        }
        return out;
    }

    std::vector<std::string> m_header;
    std::vector<std::vector<std::string>> m_rows;
};

// atof / atoi of a cell (parCsvFile::GetFloat / GetInt): leading white space,
// then the longest number at the start; 0 when there is none.
std::string_view numberStart(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r')))
        ++i;
    s.remove_prefix(i);
    if (s.starts_with('+'))
        s.remove_prefix(1);
    return s;
}

float atofCell(const std::string& cell) {
    const std::string_view s = numberStart(cell);
    double v = 0.0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return static_cast<float>(v);
}

int atoiCell(const std::string& cell) {
    const std::string_view s = numberStart(cell);
    int v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

// lvlAiMap's flags of the current road (lvlAiRoad's first word, PsdlRoad::flags).
constexpr std::uint32_t kRoadPedBlocked = 0x4;       // lvlAiMap::IsPedBlocked
constexpr std::uint32_t kRoadSidewalks = 0x40;       // the road strips have sidewalks
constexpr std::uint32_t kRoadCurbToCentre = 0x400000; // no retail road has it

// lvlAiMap's current road, as cityLevel::Load sets it up for the street props
// (lvlAiMap::SetRoad with bevel mode off): one road attribute per room of the
// road, and the sidewalk polylines GetSidewalkVertex derives from them.
class AiRoad {
public:
    AiRoad(const city::Psdl& psdl, const city::PsdlRoad& road) : m_psdl(psdl) {
        m_flags = road.flags;
        // lvlAiMap::LoadCurrent, enumerated over the road's rooms in order:
        // every road strip (4 vertices a section), rectangle strip (2) or
        // divided road (6) fills the next slot until there is one per room.
        for (const auto id : road.rooms) {
            const auto r = city::PsdlRoad::roomId(id);
            if (r >= psdl.rooms.size())
                continue;
            for (const auto& a : psdl.rooms[r].attributes) {
                if (m_rooms.size() == road.rooms.size())
                    break;
                const int stride = a.type == city::PsdlAttrType::RoadStrip          ? 4
                                   : a.type == city::PsdlAttrType::RectangleStrip   ? 2
                                   : a.type == city::PsdlAttrType::DividedRoadStrip ? 6
                                                                                    : 0;
                if (!stride)
                    continue;
                const auto v = a.vertices();
                m_rooms.push_back({v, stride, static_cast<int>(v.size()) / stride, 0});
            }
        }
        // lvlAiMap::GetRoom: the n-th room of the road, without its sign.
        for (std::size_t k = 0; k < m_rooms.size(); ++k)
            m_rooms[k].room = city::PsdlRoad::roomId(road.rooms[k]);
        // SetRoad: a road missing an attribute gets no vertices.
        if (m_rooms.size() != road.rooms.size() || m_rooms.empty())
            return;
        // GetNumVertexs (every room chopped): two per section, the first
        // section of each room after the first being the previous room's
        // last.
        const int count = static_cast<int>(m_rooms.size());
        int n = 0;
        for (int k = 0; k < count; ++k) {
            const int sections = m_rooms[static_cast<std::size_t>(k)].sections;
            if (count == 1) {
                n = n + sections * 2;
            } else {
                n = n - 2 + sections * 2;
                if (k == 0 || k == count - 1)
                    n = n + 1;
            }
        }
        m_numVertexs = n;
    }

    // lvlSDL::IsoLerp: the point `distance` metres along the left (side 1)
    // or right (side 0) sidewalk's curb or outer edge, and the room of the
    // segment it is on (the room lvlAiMap was left at by the segment's first
    // vertex).
    bool isoLerp(float distance, bool outerEdge, int side, Vec3& out, int& room) const {
        const int last = m_numVertexs - 1;
        for (int i = 0; i < last;) {
            int current = 0;
            const Vec3 a = sidewalkVertex(i, outerEdge, side, current);
            room = m_rooms[static_cast<std::size_t>(current)].room;
            ++i;
            const Vec3 b = sidewalkVertex(i, outerEdge, side, current);
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            const float length = std::sqrt((dz * dz + dy * dy) + dx * dx);
            if (distance <= length) {
                // OpenMM2: a zero-length segment hit exactly gives its point
                // (MM2 divides 0 by 0).
                const float t = length > 0.0f ? distance / length : 0.0f;
                out = {(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y, (b.z - a.z) * t + a.z};
                return true;
            }
            distance = distance - length;
        }
        return false;
    }

private:
    struct Room {
        std::span<const std::uint16_t> vertices;
        int stride = 0;
        int sections = 0;
        int room = 0;
    };

    Vec3 vertex(const Room& r, int index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= r.vertices.size())
            return {}; // OpenMM2 guard: MM2 reads past the attribute
        const auto v = r.vertices[static_cast<std::size_t>(index)];
        return v < m_psdl.vertices.size() ? m_psdl.vertices[v] : Vec3{};
    }

    // lvlAiMap::GetSidewalkVertexSingle: section k of road room r. The left
    // sidewalk (side != 0) is vertices 1 (curb) and 0 (outer edge), the
    // right one 2 and 3 (4 and 5 on divided roads); without sidewalks both
    // edges are vertex 0 on the left and 1 on the right.
    Vec3 sidewalkVertexSingle(const Room& r, int k, bool outerEdge, int side) const {
        const int base = r.stride * k;
        if (side == 0) {
            const int offset = r.stride == 6 ? 2 : 0;
            if (!(m_flags & kRoadSidewalks))
                return vertex(r, base + 1);
            return vertex(r, base + offset + (outerEdge ? 3 : 2));
        }
        if (!(m_flags & kRoadSidewalks))
            return vertex(r, base);
        return vertex(r, base + (outerEdge ? 0 : 1));
    }

    // lvlAiMap::GetVertexSingleCenter: the middle of the road at section k.
    Vec3 vertexSingleCenter(const Room& r, int k) const {
        int base = r.stride * k;
        if (r.stride == 4 || r.stride == 6)
            base = base + 1;
        const Vec3 a = vertex(r, base);
        const Vec3 b = vertex(r, base + 1 + (r.stride == 6 ? 2 : 0));
        return {(b.x - a.x) * 0.5f + a.x, (b.y - a.y) * 0.5f + a.y, (b.z - a.z) * 0.5f + a.z};
    }

    // lvlAiMap::GetSidewalkVertexMulti: section n of the whole road; sets the
    // current room.
    Vec3 sidewalkVertexMulti(int n, bool outerEdge, int side, int& current) const {
        const int count = static_cast<int>(m_rooms.size());
        int r = 0, start = 0;
        for (; r < count; ++r) {
            const int end = start + m_rooms[static_cast<std::size_t>(r)].sections - (r != 0 ? 1 : 0);
            if (n < end)
                break;
            start = end;
        }
        if (r == count)
            return {}; // MM2 reports the error and returns the origin
        current = r;
        const Room& room = m_rooms[static_cast<std::size_t>(r)];
        const int k = n - start + (r != 0 ? 1 : 0);
        if (!outerEdge && !(m_flags & kRoadPedBlocked) && (m_flags & kRoadCurbToCentre)) {
            // The curb moves 95% of the way to the middle of the road.
            const Vec3 c = vertexSingleCenter(room, k);
            const Vec3 s = sidewalkVertexSingle(room, k, false, side);
            return {(c.x - s.x) * 0.95f + s.x, (c.y - s.y) * 0.95f + s.y, (c.z - s.z) * 0.95f + s.z};
        }
        return sidewalkVertexSingle(room, k, outerEdge, side);
    }

    // lvlAiMap::GetSidewalkVertex: two vertices per section. The ends of the
    // road are its first and last sections; inside, a section's point is
    // moved towards the previous section (even index) or the next one (odd
    // index) by a third of the way, at most 0.1 m (15 m for the second and
    // second last vertex), which cuts the corners.
    Vec3 sidewalkVertex(int i, bool outerEdge, int side, int& current) const {
        float most = 0.1f;
        if (i == 1 || i == m_numVertexs - 2)
            most = 15.0f;
        const int k = i / 2;
        if (i == 0 || i == m_numVertexs - 1)
            return sidewalkVertexMulti(k, outerEdge, side, current);
        const bool odd = (i % 2) == 1;
        const Vec3 p = sidewalkVertexMulti(k, outerEdge, side, current);
        const int here = current;
        const Vec3 q = sidewalkVertexMulti(odd ? k + 1 : k - 1, outerEdge, side, current);
        if (!odd)
            current = here; // the even vertex restores the current room
        const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
        const float length = std::sqrt((dy * dy + dz * dz) + dx * dx);
        const float m2 = (dz * dz + dy * dy) + dx * dx;
        const float inv = m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
        float cut = length * 0.333f;
        if (most < cut)
            cut = most;
        const Vec3 d{inv * dx, inv * dy, inv * dz};
        return {p.x + d.x * cut, p.y + d.y * cut, p.z + d.z * cut};
    }

    const city::Psdl& m_psdl;
    std::uint32_t m_flags = 0;
    std::vector<Room> m_rooms;
    int m_numVertexs = 0;
};

} // namespace

std::vector<PropDef> parsePropDefs(std::string_view t) {
    // cityPropulator's def reader (the callback of cityPropulator::Propulate)
    // finds a def's row by its "name" column and reads start, distance,
    // minLerp, maxLerp (atof) and maxUse (atoi) by column name; the variants
    // are the cells from file1 (lvlSDL::Propulate's callback). A missing
    // column falls back to the usual position and a missing cell keeps the
    // default (MM2 quits or reads a null cell; no retail file has either).
    std::vector<PropDef> out;
    const ParCsv csv(t);
    auto col = [&](const char* name, int fallback) {
        const int c = csv.column(name);
        return c >= 0 ? c : fallback;
    };
    const int name = col("name", 0), start = col("start", 1), distance = col("distance", 2),
              maxUse = col("maxUse", 3), minLerp = col("minLerp", 4), maxLerp = col("maxLerp", 5),
              file1 = col("file1", 6);
    for (const auto& row : csv.rows()) {
        const std::string* n = ParCsv::cell(row, name);
        if (!n)
            continue; // a row without that cell (a blank line) never matches a name
        PropDef d;
        d.name = str::lower(*n);
        if (const auto* c = ParCsv::cell(row, start))
            d.start = atofCell(*c);
        if (const auto* c = ParCsv::cell(row, distance))
            d.distance = atofCell(*c);
        if (const auto* c = ParCsv::cell(row, maxUse))
            d.maxUse = atoiCell(*c);
        if (const auto* c = ParCsv::cell(row, minLerp))
            d.minLerp = atofCell(*c);
        d.maxLerp = d.minLerp;
        if (const auto* c = ParCsv::cell(row, maxLerp))
            d.maxLerp = atofCell(*c);
        // file1..file4 as far as the row has cells: an empty cell between
        // two commas is an empty name (picking it places nothing).
        for (int c = file1; c < file1 + 4; ++c)
            if (const auto* f = ParCsv::cell(row, c))
                d.files.push_back(str::lower(*f));
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<PropRule> parsePropRules(std::string_view t) {
    // cityPropulator::LookupRule finds a rule by its "rulename" column;
    // Propulate takes the cells from the "prop1" column to the last column,
    // stopping at the row's end and skipping empty names.
    std::vector<PropRule> out;
    const ParCsv csv(t);
    const int nameCol = std::max(csv.column("rulename"), 0);
    const int first = csv.column("prop1");
    const int prop1 = first >= 0 ? first : 1;
    for (const auto& row : csv.rows()) {
        const std::string* n = ParCsv::cell(row, nameCol);
        if (!n)
            continue;
        PropRule r;
        r.name = str::lower(*n);
        for (int c = prop1;; ++c) {
            const std::string* p = ParCsv::cell(row, c);
            if (!p)
                break;
            if (!p->empty())
                r.props.push_back(str::lower(*p));
        }
        out.push_back(std::move(r));
    }
    return out;
}

PathPlacement decodePathPlacement(const city::PathSetPath& path) {
    PathPlacement p;
    if (path.points.empty())
        return p;
    // dgPath::Load: type byte, then the spacing byte in quarter metres
    // (times 0.25); a spacing of 0 means 5 m.
    const std::uint32_t w = path.points.back().extra;
    p.type = static_cast<int>(w & 0xFF);
    const auto spacing = (w >> 8) & 0xFF;
    p.spacing = static_cast<float>(spacing) * 0.25f;
    if (p.spacing == 0.0f)
        p.spacing = 5.0f;
    return p;
}

std::vector<PlacedProp> placePathSet(const city::PathSet& set, PlacedProp::Source source,
                                     const BangerDataLibrary* bangerOnly) {
    std::vector<PlacedProp> out;
    constexpr Vec3 kY{0.0f, 1.0f, 0.0f};
    for (const auto& path : set.paths) {
        // Names may carry a prefix ("open:giz_bridge02_l"); no retail path
        // set placed through cityLevel::LoadPathSet has one.
        std::string model = str::lower(path.name);
        if (const auto colon = model.find(':'); colon != std::string::npos)
            model = model.substr(colon + 1);
        if (model.empty() || path.points.empty() || (bangerOnly && !bangerOnly->has(model)))
            continue;
        const PathPlacement pl = decodePathPlacement(path);
        const auto& pts = path.points;
        if (pl.type == 2) {
            // dgPath::Enumerate, line strip: each segment on its own.
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const Vec3 a = pts[i].position, b = pts[i + 1].position;
                const float lx = a.x - b.x, ly = a.y - b.y, lz = a.z - b.z;
                float remaining = std::sqrt((lz * lz + lx * lx) + ly * ly);
                Mat34 m;
                Vec3 x{b.x - a.x, b.y - a.y, b.z - a.z};
                const float xx = (x.z * x.z + x.x * x.x) + x.y * x.y;
                const float inv = xx == 0.0f ? 0.0f : 1.0f / std::sqrt(xx);
                x = {x.x * inv, x.y * inv, x.z * inv};
                m.m0 = x;
                m.m2 = x.cross(kY);        // not normalised: shorter on a slope
                m.m1 = m.m2.cross(m.m0);
                const float count = std::floor(remaining / pl.spacing);
                const float step = remaining / count;
                Vec3 at = a;
                // MM2 walks while at least the spacing is left. OpenMM2 also
                // stops after `count` props (at most a million), which only
                // matters when float steps no longer shorten the rest
                // (malformed data).
                const int most = count < 1.0e6f ? static_cast<int>(count) : 1000000;
                for (int placed = 0; remaining >= pl.spacing && placed < most; ++placed) {
                    if (zeroRow(m.m0) || zeroRow(m.m1) || zeroRow(m.m2)) {
                        log::debug("bangers: {}: bad path data, prop skipped", model);
                    } else {
                        m.m3 = at;
                        out.push_back({model, m, 0, source, true});
                    }
                    at = {x.x * step + at.x, step * x.y + at.y, x.z * step + at.z};
                    remaining = remaining - step;
                }
            }
        } else if (pl.type == 1) {
            // Position / direction pairs: +X towards the second point.
            for (std::size_t i = 0; i + 1 < pts.size(); i += 2) {
                Vec3 x = pts[i + 1].position - pts[i].position;
                x.y = 0.0f;
                if (zeroRow(x))
                    continue;
                Mat34 m;
                m.m0 = x.normalized();
                m.m1 = kY;
                m.m2 = m.m0.cross(m.m1);
                m.m3 = pts[i].position;
                out.push_back({model, m, 0, source, false});
            }
        } else if (pl.type == 0) {
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
        defByName.try_emplace(d.name, &d);
    std::map<std::string, const PropRule*, std::less<>> ruleByName;
    for (const auto& r : rules)
        ruleByName.try_emplace(r.name, &r);

    for (const auto& road : psdl.roads) {
        // Without sidewalks (flag 0x40) the curb and the outer edge are the
        // same point and nothing can stand: skipping the road changes nothing.
        if (road.rooms.empty() || !(road.flags & kRoadSidewalks))
            continue;
        const auto firstRoom = city::PsdlRoad::roomId(road.rooms.front());
        if (firstRoom >= psdl.rooms.size())
            continue;
        // The prop rule of the road's first room; rule 0 has no props.
        const int ruleNumber = psdl.rooms[firstRoom].propRule;
        if (ruleNumber == 0)
            continue;
        // MM2 quits when a rule is missing; OpenMM2 leaves that side empty.
        const PropRule* sideRules[2] = {nullptr, nullptr};
        if (auto it = ruleByName.find(std::format("n{:02}left", ruleNumber)); it != ruleByName.end())
            sideRules[0] = it->second;
        if (auto it = ruleByName.find(std::format("n{:02}right", ruleNumber)); it != ruleByName.end())
            sideRules[1] = it->second;
        if (!sideRules[0] && !sideRules[1])
            continue;

        const AiRoad ai(psdl, road);
        // lvlSDL::Enumerate over the first room: one walk per road strip or
        // divided road.
        int strips = 0;
        for (const auto& a : psdl.rooms[firstRoom].attributes)
            if (a.type == city::PsdlAttrType::RoadStrip || a.type == city::PsdlAttrType::DividedRoadStrip)
                ++strips;

        fx::Rand rng(1); // ResetRandomSeed
        // The left rule's props stand on side 1, the right rule's on side 0.
        for (int ruleIndex = 0; ruleIndex < 2; ++ruleIndex) {
            if (!sideRules[ruleIndex])
                continue;
            const int ruleSide = ruleIndex == 0 ? 1 : 0;
            for (const auto& propName : sideRules[ruleIndex]->props) {
                const auto defIt = defByName.find(propName);
                if (defIt == defByName.end())
                    continue; // MM2 quits
                const PropDef& d = *defIt->second;
                int maxUse = d.maxUse;
                for (int strip = 0; strip < strips; ++strip) {
                    // lvlSDL::Propulate: the left sidewalk, then the right.
                    for (const int side : {1, 0}) {
                        float along = d.start;
                        Vec3 curb, outer;
                        int room = 0;
                        bool ok = ai.isoLerp(along, false, side, curb, room);
                        while (ok && ai.isoLerp(along, true, side, outer, room)) {
                            const float f = (d.maxLerp - d.minLerp) * rng.frand() + d.minLerp;
                            const Vec3 at{(outer.x - curb.x) * f + curb.x, (outer.y - curb.y) * f + curb.y,
                                          (outer.z - curb.z) * f + curb.z};
                            Mat34 m;
                            m.m3 = {at.x, at.y + 0.15f, at.z};
                            Vec3 x{outer.x - curb.x, outer.y - curb.y, outer.z - curb.z};
                            const float xx = (x.z * x.z + x.y * x.y) + x.x * x.x;
                            const float inv = xx == 0.0f ? 0.0f : 1.0f / std::sqrt(xx);
                            m.m0 = {x.x * inv, x.y * inv, x.z * inv};
                            m.m1 = {0.0f, 1.0f, 0.0f};
                            m.m2 = m.m0.cross(m.m1);
                            if (!zeroRow(m.m0) && !zeroRow(m.m1) && !zeroRow(m.m2) && side == ruleSide &&
                                maxUse != 0) {
                                // cityPropulator's callback: one use, then a
                                // random variant among file1..file4 (as many
                                // as the row has cells, at least one); an
                                // empty or missing name places nothing.
                                --maxUse;
                                const int cells = static_cast<int>(d.files.size());
                                const int pick = rng.irand() % std::max(cells, 1);
                                if (pick < cells && !d.files[static_cast<std::size_t>(pick)].empty())
                                    out.push_back({d.files[static_cast<std::size_t>(pick)], m, room,
                                                   PlacedProp::Source::StreetRule, false});
                            }
                            along = (d.distance - d.distance) * rng.frand() + d.distance + along;
                            ok = ai.isoLerp(along, false, side, curb, room);
                        }
                    }
                }
            }
        }
    }
    return out;
}

std::string racePropsName(GameMode mode, int raceIndex) {
    // dgGameModeNames: "roam", "race%d", "multicop", "circuit%d", "blitz%d",
    // "croam", "crash%d".
    switch (mode) {
    case GameMode::Cruise: return "roam";
    case GameMode::CopsAndRobbers: return "multicop";
    case GameMode::Checkpoint: return raceIndex >= 0 ? std::format("race{}", raceIndex) : std::string();
    case GameMode::Circuit: return raceIndex >= 0 ? std::format("circuit{}", raceIndex) : std::string();
    case GameMode::Blitz: return raceIndex >= 0 ? std::format("blitz{}", raceIndex) : std::string();
    case GameMode::CrashCourse: return raceIndex >= 0 ? std::format("crash{}", raceIndex) : std::string();
    }
    return {};
}

std::vector<asset::PkgXref> pkgXrefs(const vfs::Vfs& vfs, std::string_view model) {
    // modPackage::OpenFile("xrefs"): the FILE chunk named "xrefs" (six
    // bytes with the NUL), in PKG3 after its size word; a count, then 80-byte
    // entries: m0, m1, m2, m3 and a 32-character name.
    std::vector<asset::PkgXref> out;
    const auto bytes = vfs.readAll(std::format("geometry/{}.pkg", str::lower(model)));
    if (!bytes || bytes->size() < 4)
        return out;
    const char* data = reinterpret_cast<const char*>(bytes->data());
    const std::string_view file(data, bytes->size());
    const bool v3 = file.starts_with("PKG3");
    if (!v3 && !file.starts_with("PKG2"))
        return out;
    constexpr std::string_view kChunk{"FILE\x06xrefs\0", 11};
    std::size_t pos = 4;
    for (;;) {
        pos = file.find(kChunk, pos);
        if (pos == std::string_view::npos)
            return out;
        pos += kChunk.size();
        if (v3)
            pos += 4;
        if (pos + 4 > file.size())
            return out;
        std::uint32_t count = 0;
        std::memcpy(&count, data + pos, 4);
        pos += 4;
        if (static_cast<std::size_t>(count) * 80 > file.size() - pos)
            continue; // not the chunk after all
        out.resize(count);
        for (auto& x : out) {
            float f[12];
            std::memcpy(f, data + pos, sizeof f);
            x.transform.m0 = {f[0], f[1], f[2]};
            x.transform.m1 = {f[3], f[4], f[5]};
            x.transform.m2 = {f[6], f[7], f[8]};
            x.transform.m3 = {f[9], f[10], f[11]};
            const char* name = data + pos + 48;
            x.name = std::string(name, strnlen(name, 32));
            pos += 80;
        }
        return out;
    }
}

std::vector<PlacedProp> placeXrefs(const city::Instance& record, const std::vector<asset::PkgXref>& xrefs,
                                   const BangerDataLibrary& data) {
    std::vector<PlacedProp> out;
    auto zero = [](const Vec3& v) { return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f; };
    auto unit = [](Vec3& v) {
        const float m2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
        if (m2 < 0.97f || m2 > 1.03f) {
            const float s = m2 == 0.0f ? 0.0f : 1.0f / std::sqrt(m2);
            v = {s * v.x, s * v.y, s * v.z};
        }
    };
    for (const auto& x : xrefs) {
        Mat34 m = x.transform;
        cam::dot(m, record.transform); // Matrix34::Dot(xref, record)
        if (zero(m.m0) || zero(m.m1) || zero(m.m2)) {
            log::warn("bangers: {}: bad x-ref matrix", record.name);
            continue;
        }
        if ((m.m0.y * m.m1.y + m.m0.z * m.m1.z) + m.m1.x * m.m0.x > 0.01f ||
            (m.m2.x * m.m1.x + m.m2.y * m.m1.y) + m.m2.z * m.m1.z > 0.01f ||
            (m.m0.y * m.m2.y + m.m0.z * m.m2.z) + m.m2.x * m.m0.x > 0.01f) {
            log::warn("bangers: {}: really bad x-ref matrix", record.name);
            continue;
        }
        unit(m.m0);
        unit(m.m1);
        unit(m.m2);
        if (!data.has(x.name)) {
            log::debug("bangers: x-ref {} of {} has no banger data", x.name, record.name);
            continue;
        }
        PlacedProp p{str::lower(x.name), m, 0, PlacedProp::Source::Instance, true, record.flags & 0xFF};
        p.roomHint = record.room;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PlacedProp> placeCityProps(const city::CityData& city, const vfs::Vfs& vfs,
                                       const BangerDataLibrary& data, std::string_view raceProps) {
    std::vector<PlacedProp> out;
    const std::string map = str::lower(city.info.mapName);
    const std::string dir = "city/" + map + "/";
    auto addPathSet = [&](const std::string& path, PlacedProp::Source source) {
        auto bytes = vfs.readAll(path);
        if (!bytes)
            return; // dgPathSet::Load: no file, no props
        std::string error;
        if (auto set = city::parsePathSet(*bytes, &error)) {
            auto props = placePathSet(*set, source, &data);
            out.insert(out.end(), props.begin(), props.end());
        } else {
            log::warn("bangers: {}: {}", path, error);
        }
    };

    // cityPropulator: the street rules.
    const auto defs = vfs.readAll(dir + "propdefs.csv");
    const auto rules = vfs.readAll(dir + "proprules.csv");
    if (defs && rules) {
        auto props = placeStreetProps(city.psdl, parsePropDefs(text(*defs)), parsePropRules(text(*rules)));
        out.insert(out.end(), props.begin(), props.end());
    }
    // lvlLevel::LoadInstances of <map>.inst and <map>_ai.inst: banger records
    // keep their matrix unless stored in the compact Y rotation form, and
    // take the record's variant byte. MM2 picks them by the record's banger
    // flag (0x200); OpenMM2 by their banger data, which retail data marks
    // exactly alike (and CityLevel and CityRenderer skip by name too).
    // After each record come the bangers its geometry's xrefs place (the
    // "xrefs" chunk of its PKG; the name after the last backslash, CleanName).
    std::map<std::string, std::vector<asset::PkgXref>, std::less<>> xrefCache;
    for (const auto* list : {&city.instances, &city.aiInstances})
        for (const auto& inst : *list) {
            if (data.has(inst.name))
                out.push_back({str::lower(inst.name), inst.transform, inst.room, PlacedProp::Source::Instance,
                               !inst.rotY, inst.flags & 0xFF});
            std::string geom = str::lower(inst.name);
            if (const auto slash = geom.rfind('\\'); slash != std::string::npos)
                geom = geom.substr(slash + 1);
            auto it = xrefCache.find(geom);
            if (it == xrefCache.end())
                it = xrefCache.emplace(geom, pkgXrefs(vfs, geom)).first;
            if (!it->second.empty()) {
                auto props = placeXrefs(inst, it->second, data);
                out.insert(out.end(), props.begin(), props.end());
            }
        }
    // cityLevel::LoadPathSet("city/<map>", "props").
    addPathSet(dir + "props.pathset", PlacedProp::Source::PathSet);
    // The race's props: cityLevel::LoadPathSet("race/<map>", <race name>).
    if (!raceProps.empty())
        addPathSet(std::format("race/{}/{}.pathset", map, str::lower(raceProps)), PlacedProp::Source::Race);
    return out;
}

} // namespace mm2::game::bangers
