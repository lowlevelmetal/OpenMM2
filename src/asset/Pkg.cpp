#include "asset/Pkg.h"

#include "asset/Reader.h"
#include "core/StringUtil.h"

#include <cstring>
#include <format>

namespace mm2::asset {
namespace {

using detail::Reader;

bool fail(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
    return false;
}

// Sanity limits well above anything in the retail data.
constexpr std::uint32_t kMaxSections = 4096;
constexpr std::uint32_t kMaxPackets = 4096;
constexpr std::uint32_t kMaxVerts = 65536;
constexpr std::uint32_t kMaxIndices = 1u << 20;
constexpr std::uint32_t kMaxShaders = 4096;

bool parseGeometry(Reader& r, PkgMesh& mesh, std::vector<std::string>& warnings, std::string* error) {
    const std::uint32_t sectionCount = r.u32();
    const std::uint32_t totalVerts = r.u32();
    const std::uint32_t totalIndices = r.u32();
    const std::uint32_t sectionCount2 = r.u32();
    mesh.fvf = r.u32();
    if (!r.ok())
        return fail(error, "truncated geometry header");
    if (sectionCount > kMaxSections)
        return fail(error, std::format("implausible section count {}", sectionCount));
    const std::size_t stride = fvfVertexSize(mesh.fvf);
    if (stride == 0)
        return fail(error, std::format("unsupported vertex format 0x{:x}", mesh.fvf));

    const bool hasNormal = mesh.fvf & Fvf::Normal;
    const bool hasDiffuse = mesh.fvf & Fvf::Diffuse;
    const bool hasSpecular = mesh.fvf & Fvf::Specular;
    const std::uint32_t texCount = (mesh.fvf & Fvf::TexCountMask) >> Fvf::TexCountShift;

    std::size_t vertsSeen = 0, indicesSeen = 0;
    mesh.sections.resize(sectionCount);
    for (auto& section : mesh.sections) {
        const std::uint16_t packetCount = r.u16();
        section.flags = r.u16();
        section.shaderIndex = r.u32();
        if (!r.ok() || packetCount > kMaxPackets)
            return fail(error, "truncated or corrupt section header");
        section.packets.resize(packetCount);
        for (auto& packet : section.packets) {
            packet.primitive = r.u32();
            const std::uint32_t vertCount = r.u32();
            if (!r.ok() || vertCount > kMaxVerts || !r.has(std::size_t{vertCount} * stride))
                return fail(error, "truncated or corrupt vertex array");
            packet.vertices.resize(vertCount);
            for (auto& v : packet.vertices) {
                const std::size_t start = r.pos();
                v.position = r.vec3();
                if (hasNormal)
                    v.normal = r.vec3();
                if (hasDiffuse)
                    v.color = r.u32();
                if (hasSpecular)
                    r.skip(4);
                if (texCount > 0)
                    v.uv = r.vec2();
                r.seek(start + stride);
            }
            const std::uint32_t indexCount = r.u32();
            if (!r.ok() || indexCount > kMaxIndices || !r.has(std::size_t{indexCount} * 2))
                return fail(error, "truncated or corrupt index array");
            packet.indices.resize(indexCount);
            for (auto& i : packet.indices) {
                i = r.u16();
                if (i >= vertCount)
                    return fail(error, std::format("index {} out of range ({} vertices)", i, vertCount));
            }
            if (packet.primitive == 3 && indexCount % 3 != 0)
                return fail(error, std::format("triangle list with {} indices", indexCount));
            vertsSeen += vertCount;
            indicesSeen += indexCount;
        }
    }
    if (sectionCount2 != sectionCount || totalVerts != vertsSeen || totalIndices != indicesSeen)
        warnings.push_back(std::format("{}: header totals ({} sections, {} vertices, {} indices) disagree with "
                                       "contents ({}, {}, {})",
                                       mesh.name, sectionCount2, totalVerts, totalIndices, sectionCount, vertsSeen,
                                       indicesSeen));
    return true;
}

bool parseShaders(Reader& r, Pkg& pkg, std::string* error) {
    pkg.shaderType = r.u32();
    pkg.shadersPerPaintjob = r.u32();
    if (!r.ok() || pkg.shadersPerPaintjob > kMaxShaders)
        return fail(error, "truncated or corrupt shader header");
    const bool compact = pkg.shaderType & 0x80;
    const std::uint32_t paintjobs = pkg.shaderType & 0x7F;
    pkg.paintjobs.resize(paintjobs);
    for (auto& pj : pkg.paintjobs) {
        pj.resize(pkg.shadersPerPaintjob);
        for (auto& m : pj) {
            const std::uint8_t len = r.u8();
            m.texture = r.fixedString(len);
            if (compact) {
                auto color = [&] {
                    const float rr = r.u8() / 255.0f, gg = r.u8() / 255.0f, bb = r.u8() / 255.0f;
                    return Vec4{rr, gg, bb, r.u8() / 255.0f};
                };
                m.diffuse = color();
                m.ambient = color();
                m.specular = color();
                m.emissive = {0, 0, 0, 0};
            } else {
                m.diffuse = r.vec4();
                m.ambient = r.vec4();
                m.specular = r.vec4();
                m.emissive = r.vec4();
            }
            m.shininess = r.f32();
            if (!r.ok())
                return fail(error, "truncated shader table");
        }
    }
    return true;
}

bool parseXrefs(Reader& r, Pkg& pkg, std::string* error) {
    const std::uint32_t count = r.u32();
    if (!r.ok() || !r.has(std::size_t{count} * 80))
        return fail(error, "truncated xref table");
    pkg.xrefs.resize(count);
    for (auto& x : pkg.xrefs) {
        x.transform.m0 = r.vec3();
        x.transform.m1 = r.vec3();
        x.transform.m2 = r.vec3();
        x.transform.m3 = r.vec3();
        x.name = r.fixedString(32);
    }
    return r.ok();
}

// Parses the body of chunk `name` starting at r.pos().
bool parseChunk(Reader& r, const std::string& name, Pkg& pkg, std::string* error) {
    if (str::iequals(name, "shaders"))
        return parseShaders(r, pkg, error);
    if (str::iequals(name, "offset")) {
        pkg.offset = r.vec3();
        return r.ok() || fail(error, "truncated offset");
    }
    if (str::iequals(name, "xrefs"))
        return parseXrefs(r, pkg, error);
    PkgMesh mesh;
    mesh.name = name;
    auto [part, lod] = splitLodName(name);
    mesh.part = std::move(part);
    mesh.lod = lod;
    if (!parseGeometry(r, mesh, pkg.warnings, error))
        return false;
    pkg.meshes.push_back(std::move(mesh));
    return true;
}

bool atChunkBoundary(const Reader& r, std::span<const std::byte> data) {
    return r.pos() == data.size() ||
           (r.remaining() >= 4 && std::memcmp(data.data() + r.pos(), "FILE", 4) == 0);
}

} // namespace

std::size_t fvfVertexSize(std::uint32_t fvf) {
    std::size_t size = 0;
    if ((fvf & Fvf::PositionMask) != Fvf::Xyz)
        return 0; // only untransformed XYZ positions are supported
    size += 12;
    if (fvf & Fvf::Normal)
        size += 12;
    if (fvf & 0x020) // point size
        size += 4;
    if (fvf & Fvf::Diffuse)
        size += 4;
    if (fvf & Fvf::Specular)
        size += 4;
    // Texture coordinate sets; D3DFVF_TEXCOORDSIZE bits (16+) select sizes.
    const std::uint32_t texCount = (fvf & Fvf::TexCountMask) >> Fvf::TexCountShift;
    for (std::uint32_t i = 0; i < texCount; ++i) {
        switch ((fvf >> (16 + i * 2)) & 3) {
        case 0: size += 8; break;  // 2D
        case 1: size += 12; break; // 3D
        case 2: size += 16; break; // 4D
        case 3: size += 4; break;  // 1D
        }
    }
    return size;
}

std::size_t PkgMesh::vertexCount() const {
    std::size_t n = 0;
    for (const auto& s : sections)
        for (const auto& p : s.packets)
            n += p.vertices.size();
    return n;
}

std::size_t PkgMesh::triangleCount() const {
    std::size_t n = 0;
    for (const auto& s : sections)
        for (const auto& p : s.packets)
            n += p.indices.size() / 3;
    return n;
}

Aabb PkgMesh::bounds() const {
    Aabb b;
    for (const auto& s : sections)
        for (const auto& p : s.packets)
            for (const auto& v : p.vertices)
                b.expand(v.position);
    return b;
}

const PkgMesh* Pkg::find(std::string_view name) const {
    for (const auto& m : meshes)
        if (str::iequals(m.name, name))
            return &m;
    return nullptr;
}

const PkgMesh* Pkg::find(std::string_view part, Lod lod) const {
    for (const auto& m : meshes)
        if (m.lod == lod && str::iequals(m.part, part))
            return &m;
    return nullptr;
}

const PkgMesh* Pkg::findBest(std::string_view part, Lod lod) const {
    // Preferred LOD first, then progressively more detailed, then less.
    const int want = static_cast<int>(lod == Lod::None ? Lod::High : lod);
    for (int l = want; l >= 0; --l)
        if (const auto* m = find(part, static_cast<Lod>(l)))
            return m;
    for (int l = want + 1; l <= static_cast<int>(Lod::VeryLow); ++l)
        if (const auto* m = find(part, static_cast<Lod>(l)))
            return m;
    return find(part, Lod::None);
}

std::vector<std::string> Pkg::parts() const {
    std::vector<std::string> out;
    for (const auto& m : meshes) {
        bool seen = false;
        for (const auto& p : out)
            seen = seen || str::iequals(p, m.part);
        if (!seen)
            out.push_back(m.part);
    }
    return out;
}

std::string_view lodSuffix(Lod lod) {
    switch (lod) {
    case Lod::High: return "H";
    case Lod::Medium: return "M";
    case Lod::Low: return "L";
    case Lod::VeryLow: return "VL";
    case Lod::None: return "";
    }
    return "";
}

std::pair<std::string, Lod> splitLodName(std::string_view name) {
    auto lodOf = [](std::string_view s) -> Lod {
        if (str::iequals(s, "H"))
            return Lod::High;
        if (str::iequals(s, "M"))
            return Lod::Medium;
        if (str::iequals(s, "L"))
            return Lod::Low;
        if (str::iequals(s, "VL"))
            return Lod::VeryLow;
        return Lod::None;
    };
    if (const Lod whole = lodOf(name); whole != Lod::None)
        return {"", whole};
    const auto us = name.rfind('_');
    if (us != std::string_view::npos) {
        if (const Lod l = lodOf(name.substr(us + 1)); l != Lod::None)
            return {std::string(name.substr(0, us)), l};
    }
    return {std::string(name), Lod::None};
}

bool isKnownBrokenRetailAsset(std::string_view path) {
    for (const char* p : {"geometry/thing.pkg", "geometry/wf_hse_baywin03_stone_wht_5s_12_l.pkg",
                          "geometry/ww_bldg1_stone_tan_t6_20_l.pkg"})
        if (str::iequals(path, p))
            return true;
    return false;
}

std::optional<Pkg> parsePkg(std::span<const std::byte> data, std::string* error) {
    if (data.size() < 4) {
        fail(error, "file too small for a PKG header");
        return std::nullopt;
    }
    Pkg pkg;
    if (std::memcmp(data.data(), "PKG3", 4) == 0)
        pkg.version = 3;
    else if (std::memcmp(data.data(), "PKG2", 4) == 0)
        pkg.version = 2;
    else {
        fail(error, "not a PKG file");
        return std::nullopt;
    }

    Reader r(data, 4);
    while (r.remaining() > 0) {
        if (r.remaining() < 5 || std::memcmp(data.data() + r.pos(), "FILE", 4) != 0) {
            fail(error, std::format("expected FILE chunk at offset {}", r.pos()));
            return std::nullopt;
        }
        r.skip(4);
        const std::uint8_t nameLen = r.u8();
        const std::string name = r.fixedString(nameLen);
        if (!r.ok()) {
            fail(error, "truncated chunk name");
            return std::nullopt;
        }

        std::string chunkError;
        if (pkg.version == 3) {
            const std::uint32_t declared = r.u32();
            const std::size_t start = r.pos();
            if (r.ok() && declared > 0 && declared <= r.remaining()) {
                Reader sub(data.first(start + declared), start);
                const std::size_t mark = pkg.meshes.size();
                if (parseChunk(sub, name, pkg, &chunkError) && sub.pos() == start + declared) {
                    r.seek(start + declared);
                    continue;
                }
                pkg.meshes.resize(mark);
            }
            // The declared size is wrong (a few retail files have a zero or
            // garbage size): fall back to parsing the chunk structurally.
            Reader free(data, start);
            if (parseChunk(free, name, pkg, &chunkError) && atChunkBoundary(free, data)) {
                pkg.warnings.push_back(std::format("{}: declared size {} is wrong, parsed {} bytes", name, declared,
                                                   free.pos() - start));
                r.seek(free.pos());
                continue;
            }
            fail(error, std::format("chunk '{}': {}", name, chunkError.empty() ? "size mismatch" : chunkError));
            return std::nullopt;
        }

        // PKG2: chunks carry no size.
        if (!parseChunk(r, name, pkg, &chunkError) || !atChunkBoundary(r, data)) {
            fail(error, std::format("chunk '{}': {}", name, chunkError.empty() ? "trailing data" : chunkError));
            return std::nullopt;
        }
    }
    return pkg;
}

} // namespace mm2::asset
