#include "city/Psdl.h"

#include "city/Reader.h"

#include <format>

namespace mm2::city {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

bool hasCountWord(PsdlAttrType t) {
    switch (t) {
    case PsdlAttrType::RoadStrip:
    case PsdlAttrType::SidewalkStrip:
    case PsdlAttrType::RectangleStrip:
    case PsdlAttrType::RoadTriangleFan:
    case PsdlAttrType::TriangleFan:
    case PsdlAttrType::DividedRoadStrip:
    case PsdlAttrType::Tunnel:
    case PsdlAttrType::RoofTriangleFan:
        return true;
    default:
        return false;
    }
}

// Number of argument words for an attribute whose element count is `n`.
std::size_t argWords(PsdlAttrType t, std::size_t n) {
    switch (t) {
    case PsdlAttrType::RoadStrip:
        return 4 * n;
    case PsdlAttrType::SidewalkStrip:
    case PsdlAttrType::RectangleStrip:
        return 2 * n;
    case PsdlAttrType::RoadTriangleFan:
    case PsdlAttrType::TriangleFan:
    case PsdlAttrType::RoofTriangleFan:
        return n + 2; // roof: height + n+1 vertices
    case PsdlAttrType::DividedRoadStrip:
        return 2 + 6 * n;
    case PsdlAttrType::Tunnel:
        return n;
    case PsdlAttrType::Sliver:
    case PsdlAttrType::Crosswalk:
    case PsdlAttrType::FacadeBound:
        return 4;
    case PsdlAttrType::Texture:
        return 1;
    case PsdlAttrType::Facade:
        return 6;
    }
    return 0;
}

} // namespace

const char* psdlAttrTypeName(PsdlAttrType t) {
    static constexpr const char* names[kPsdlAttrTypeCount] = {
        "RoadStrip",       "SidewalkStrip", "RectangleStrip", "Sliver",           "Crosswalk",
        "RoadTriangleFan", "TriangleFan",   "FacadeBound",    "DividedRoadStrip", "Tunnel",
        "Texture",         "Facade",        "RoofTriangleFan"};
    const auto i = static_cast<int>(t);
    return i < kPsdlAttrTypeCount ? names[i] : "?";
}

std::span<const std::uint16_t> PsdlAttribute::counted() const {
    std::span<const std::uint16_t> s(args);
    if (hasCountWord(type) && subtype == 0 && !s.empty())
        s = s.subspan(1);
    return s;
}

int PsdlAttribute::textureBase() const {
    if (type != PsdlAttrType::Texture || args.empty())
        return -1;
    return ((static_cast<int>(subtype) << 8) | args[0]) - 1;
}

std::span<const std::uint16_t> PsdlAttribute::vertices() const {
    const auto s = counted();
    switch (type) {
    case PsdlAttrType::RoofTriangleFan:
        return s.empty() ? s : s.subspan(1);
    case PsdlAttrType::DividedRoadStrip:
        return s.size() < 2 ? s.subspan(s.size()) : s.subspan(2);
    case PsdlAttrType::Sliver:
    case PsdlAttrType::FacadeBound:
    case PsdlAttrType::Facade:
        return s.size() >= 2 ? s.last(2) : s;
    case PsdlAttrType::Texture:
    case PsdlAttrType::Tunnel:
        return {};
    default:
        return s;
    }
}

bool decodePsdlAttributes(std::span<const std::uint16_t> words, std::vector<PsdlAttribute>& out,
                          std::string* error) {
    out.clear();
    std::size_t i = 0;
    while (i < words.size()) {
        const std::uint16_t header = words[i++];
        const unsigned typeBits = (header >> 3) & 0x0F;
        if (typeBits >= kPsdlAttrTypeCount) {
            setError(error, std::format("unknown attribute type {} at word {}", typeBits, i - 1));
            return false;
        }
        PsdlAttribute a;
        a.type = static_cast<PsdlAttrType>(typeBits);
        a.subtype = static_cast<std::uint8_t>(header & 0x07);
        a.last = (header & 0x80) != 0;

        std::size_t n = a.subtype;
        std::size_t extra = 0;
        if (hasCountWord(a.type) && a.subtype == 0) {
            if (i >= words.size()) {
                setError(error, "attribute count word missing");
                return false;
            }
            n = words[i];
            extra = 1;
        }
        const std::size_t len = extra + argWords(a.type, n);
        if (i + len > words.size()) {
            setError(error, std::format("{} attribute overruns the room ({} words needed, {} left)",
                                        psdlAttrTypeName(a.type), len, words.size() - i));
            return false;
        }
        a.args.assign(words.begin() + static_cast<std::ptrdiff_t>(i),
                      words.begin() + static_cast<std::ptrdiff_t>(i + len));
        i += len;
        out.push_back(std::move(a));
    }
    return true;
}

std::optional<Psdl> parsePsdl(std::span<const std::byte> data, std::string* error) {
    detail::Reader r(data);
    if (!r.magic("PSD0")) {
        setError(error, "not a PSDL file (missing PSD0)");
        return std::nullopt;
    }
    Psdl p;
    p.version = r.u32();
    if (p.version != 2) {
        setError(error, std::format("unsupported PSDL version {}", p.version));
        return std::nullopt;
    }

    const std::uint32_t numVertices = r.u32();
    r.readArray(numVertices, 12, p.vertices, [&] { return r.vec3(); });
    const std::uint32_t numHeights = r.u32();
    r.readArray(numHeights, 4, p.heights, [&] { return r.f32(); });

    // The texture count is one more than the number of names stored.
    const std::uint32_t numTextures = r.u32();
    if (!r.ok() || numTextures == 0 || numTextures - 1 > r.remaining()) {
        setError(error, "corrupt PSDL header");
        return std::nullopt;
    }
    p.textures.reserve(numTextures - 1);
    for (std::uint32_t i = 0; i + 1 < numTextures && r.ok(); ++i) {
        const std::uint8_t len = r.u8(); // includes the terminating NUL
        p.textures.push_back(len ? r.fixedString(len) : std::string());
    }

    // Room count includes the dummy room 0, which has no record.
    const std::uint32_t numRooms = r.u32();
    p.firstRoadRoom = r.u32();
    if (!r.ok() || numRooms == 0 || numRooms - 1 > r.remaining() / 8) {
        setError(error, "corrupt PSDL room count");
        return std::nullopt;
    }
    p.rooms.resize(numRooms);
    std::vector<std::uint16_t> words;
    for (std::uint32_t room = 1; room < numRooms; ++room) {
        const std::uint32_t numPerimeter = r.u32();
        const std::uint32_t numWords = r.u32();
        if (!r.ok() || numPerimeter > r.remaining() / 4 || numWords > r.remaining() / 2) {
            setError(error, std::format("room {}: truncated", room));
            return std::nullopt;
        }
        auto& rm = p.rooms[room];
        rm.perimeter.resize(numPerimeter);
        for (auto& pt : rm.perimeter) {
            pt.vertex = r.u16();
            pt.neighbor = r.u16();
        }
        words.resize(numWords);
        for (auto& w : words)
            w = r.u16();
        std::string attrError;
        if (!r.ok() || !decodePsdlAttributes(words, rm.attributes, &attrError)) {
            setError(error, std::format("room {}: {}", room, r.ok() ? attrError : "truncated"));
            return std::nullopt;
        }
    }
    for (auto& rm : p.rooms)
        rm.flags = r.u8();
    for (auto& rm : p.rooms)
        rm.propRule = r.u8();

    p.bounds.min = r.vec3();
    p.bounds.max = r.vec3();
    p.sphereCenter = r.vec3();
    p.sphereRadius = r.f32();

    const std::uint32_t numRoads = r.u32();
    if (!r.ok() || numRoads > r.remaining() / 25) {
        setError(error, "corrupt PSDL road table");
        return std::nullopt;
    }
    p.roads.resize(numRoads);
    for (auto& road : p.roads) {
        road.flags = r.u8();
        road.unknown1 = r.u8();
        road.propRule = r.u16();
        const std::uint8_t nLeft = r.u8();
        const std::uint8_t nRight = r.u8();
        for (int i = 0; i < nLeft; ++i)
            road.leftValues.push_back(r.f32());
        for (int i = 0; i < nRight; ++i)
            road.rightValues.push_back(r.f32());
        road.unknown2 = r.u8();
        road.unknown3 = r.u8();
        for (auto& v : road.startCrossroads)
            v = r.u16();
        for (auto& v : road.endCrossroads)
            v = r.u16();
        const std::uint8_t nRooms = r.u8();
        for (int i = 0; i < nRooms; ++i)
            road.rooms.push_back(r.i16());
        if (!r.ok())
            break;
    }
    if (!r.ok()) {
        setError(error, "truncated PSDL file");
        return std::nullopt;
    }
    if (!r.atEnd()) {
        setError(error, std::format("{} unexpected trailing bytes", r.remaining()));
        return std::nullopt;
    }
    return p;
}

std::vector<std::string> validatePsdl(const Psdl& p) {
    std::vector<std::string> problems;
    auto report = [&](std::string s) {
        if (problems.size() < 64)
            problems.push_back(std::move(s));
    };
    const std::size_t nv = p.vertices.size(), nh = p.heights.size(), nr = p.rooms.size();
    auto checkVert = [&](std::size_t room, std::uint16_t v, const char* what) {
        if (v >= nv)
            report(std::format("room {}: {} vertex {} out of range", room, what, v));
    };
    auto checkHeight = [&](std::size_t room, std::uint16_t h, const char* what) {
        if (h >= nh)
            report(std::format("room {}: {} height {} out of range", room, what, h));
    };
    for (std::size_t room = 1; room < nr; ++room) {
        const auto& rm = p.rooms[room];
        for (const auto& pt : rm.perimeter) {
            checkVert(room, pt.vertex, "perimeter");
            if (pt.neighbor >= nr)
                report(std::format("room {}: neighbor {} out of range", room, pt.neighbor));
        }
        for (const auto& a : rm.attributes) {
            const char* name = psdlAttrTypeName(a.type);
            switch (a.type) {
            case PsdlAttrType::Texture:
                if (a.textureBase() >= static_cast<int>(p.textures.size()))
                    report(std::format("room {}: texture {} out of range", room, a.textureBase()));
                break;
            case PsdlAttrType::Tunnel:
                if (a.args.size() < (a.subtype == 0 ? 4u : 3u))
                    report(std::format("room {}: short tunnel attribute", room));
                break;
            case PsdlAttrType::Facade:
                checkHeight(room, a.facadeBottom(), name);
                checkHeight(room, a.facadeTop(), name);
                break;
            case PsdlAttrType::Sliver:
                checkHeight(room, a.sliverTop(), name);
                break;
            case PsdlAttrType::FacadeBound:
                checkHeight(room, a.facadeBoundTop(), name);
                break;
            case PsdlAttrType::RoofTriangleFan:
                checkHeight(room, a.roofHeight(), name);
                break;
            default:
                break;
            }
            for (std::uint16_t v : a.vertices())
                checkVert(room, v, name);
        }
    }
    for (std::size_t i = 0; i < p.roads.size(); ++i) {
        const auto& road = p.roads[i];
        for (auto v : road.startCrossroads)
            if (v >= nv)
                report(std::format("road {}: crossroad vertex {} out of range", i, v));
        for (auto v : road.endCrossroads)
            if (v >= nv)
                report(std::format("road {}: crossroad vertex {} out of range", i, v));
        for (auto room : road.rooms)
            if (PsdlRoad::roomId(room) == 0 || PsdlRoad::roomId(room) >= nr)
                report(std::format("road {}: room {} out of range", i, room));
    }
    return problems;
}

} // namespace mm2::city
