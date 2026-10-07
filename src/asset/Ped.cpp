#include "asset/Ped.h"

#include "asset/Reader.h"
#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace mm2::asset {
namespace {

bool fail(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
    return false;
}

std::string_view asText(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Whitespace-separated tokens of one line ('\r' tolerated).
std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    for (auto l : str::split(text, '\n')) {
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        out.push_back(l);
    }
    return out;
}

bool toFloat(std::string_view s, float& out) {
    auto d = str::parseDouble(s);
    if (!d)
        return false;
    out = static_cast<float>(*d);
    return true;
}

bool toInt(std::string_view s, long long& out) {
    auto v = str::parseInt(s);
    if (!v)
        return false;
    out = *v;
    return true;
}

// Parses tokens[first..first+n) as floats.
bool floats(const std::vector<std::string_view>& t, std::size_t first, std::size_t n, float* out) {
    if (t.size() < first + n)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        if (!toFloat(t[first + i], out[i]))
            return false;
    return true;
}

bool ints(const std::vector<std::string_view>& t, std::size_t first, std::size_t n, long long* out) {
    if (t.size() < first + n)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        if (!toInt(t[first + i], out[i]))
            return false;
    return true;
}

std::string stem(std::string_view s) {
    s = str::trim(s);
    if (str::iendsWith(s, ".anim"))
        s.remove_suffix(5);
    return str::lower(s);
}

} // namespace

// --- Skeleton ----------------------------------------------------------------------------

int Skeleton::find(std::string_view name) const {
    for (std::size_t i = 0; i < bones.size(); ++i)
        if (str::iequals(bones[i].name, name))
            return static_cast<int>(i);
    return -1;
}

std::optional<Skeleton> parseSkeleton(std::string_view text, std::string* error) {
    // Token stream with braces split off.
    std::vector<std::string> toks;
    for (auto line : lines(text))
        for (auto t : tokens(line)) {
            std::string cur;
            for (char c : t) {
                if (c == '{' || c == '}') {
                    if (!cur.empty())
                        toks.push_back(std::move(cur));
                    cur.clear();
                    toks.emplace_back(1, c);
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
                toks.push_back(std::move(cur));
        }

    Skeleton skel;
    long long declared = -1;
    std::vector<int> stack;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        if (str::iequals(t, "NumBones")) {
            if (i + 1 >= toks.size() || !toInt(toks[i + 1], declared) || declared < 0) {
                fail(error, "bad NumBones");
                return std::nullopt;
            }
            ++i;
        } else if (str::iequals(t, "bone")) {
            if (i + 2 >= toks.size() || toks[i + 2] != "{") {
                fail(error, "expected 'bone <name> {'");
                return std::nullopt;
            }
            Skeleton::Bone b;
            b.name = toks[i + 1];
            b.parent = stack.empty() ? -1 : stack.back();
            stack.push_back(static_cast<int>(skel.bones.size()));
            skel.bones.push_back(std::move(b));
            i += 2;
        } else if (str::iequals(t, "offset")) {
            float v[3];
            if (stack.empty() || i + 3 >= toks.size() || !toFloat(toks[i + 1], v[0]) || !toFloat(toks[i + 2], v[1]) ||
                !toFloat(toks[i + 3], v[2])) {
                fail(error, "bad offset");
                return std::nullopt;
            }
            skel.bones[static_cast<std::size_t>(stack.back())].offset = {v[0], v[1], v[2]};
            i += 3;
        } else if (t == "}") {
            if (stack.empty()) {
                fail(error, "unbalanced '}'");
                return std::nullopt;
            }
            stack.pop_back();
        } else {
            fail(error, std::format("unexpected token '{}'", t));
            return std::nullopt;
        }
    }
    if (!stack.empty()) {
        fail(error, "unterminated bone block");
        return std::nullopt;
    }
    if (skel.bones.empty() || (declared >= 0 && static_cast<std::size_t>(declared) != skel.bones.size())) {
        fail(error, std::format("NumBones {} but {} bones defined", declared, skel.bones.size()));
        return std::nullopt;
    }
    return skel;
}

// --- Animation ---------------------------------------------------------------------------

Vec3 PedAnimation::rootTranslation(std::uint32_t frame) const {
    const float* f = channels.data() + static_cast<std::size_t>(frame) * channelCount;
    return {f[0], f[1], f[2]};
}

Vec3 PedAnimation::boneRotation(std::uint32_t frame, std::size_t bone) const {
    const float* f = channels.data() + static_cast<std::size_t>(frame) * channelCount + 3 + bone * 3;
    return {f[0], f[1], f[2]};
}

std::optional<PedAnimation> parsePedAnimation(std::span<const std::byte> data, std::string* error) {
    detail::Reader r(data);
    PedAnimation a;
    a.reserved = r.u32();
    a.frameCount = r.u32();
    a.channelCount = r.u32();
    a.cycleDistance = r.f32();
    a.flags = r.u8();
    if (!r.ok()) {
        fail(error, "file too small for an animation header");
        return std::nullopt;
    }
    if (a.frameCount == 0 || a.frameCount > 100000 || a.channelCount < 3 || a.channelCount > 1024 ||
        (a.channelCount - 3) % 3 != 0) {
        fail(error, std::format("implausible header: {} frames x {} channels", a.frameCount, a.channelCount));
        return std::nullopt;
    }
    const std::size_t count = static_cast<std::size_t>(a.frameCount) * a.channelCount;
    if (r.remaining() != count * 4) {
        fail(error, std::format("expected {} bytes of channel data, found {}", count * 4, r.remaining()));
        return std::nullopt;
    }
    a.channels.resize(count);
    for (auto& c : a.channels)
        c = r.f32();
    for (float c : a.channels)
        if (!std::isfinite(c)) {
            fail(error, "non-finite channel value");
            return std::nullopt;
        }
    return a;
}

Mat34 matrixFromEulersXZY(const Vec3& e) {
    // Port of Matrix34__FromEulersXZY (Open1560 game.asm), keeping its
    // operation grouping. Zero angles skip the trig calls, as the original does.
    const float sx = e.x == 0.0f ? 0.0f : std::sin(e.x);
    const float cx = e.x == 0.0f ? 1.0f : std::cos(e.x);
    const float sy = e.y == 0.0f ? 0.0f : std::sin(e.y);
    const float cy = e.y == 0.0f ? 1.0f : std::cos(e.y);
    const float sz = e.z == 0.0f ? 0.0f : std::sin(e.z);
    const float cz = e.z == 0.0f ? 1.0f : std::cos(e.z);
    const float cycx = cy * cx;
    Mat34 m;
    m.m0 = {cz * cy, sz, -(cz * sy)};
    m.m1 = {sy * sx - sz * cycx, cz * cx, (cx * sz) * sy + cy * sx};
    m.m2 = {(cy * sz) * sx + cx * sy, -(cz * sx), cycx - (sz * sy) * sx};
    m.m3 = {};
    return m;
}

void posePed(const Skeleton& skeleton, const PedAnimation* anim, float frame, std::vector<Mat34>& out) {
    const std::size_t n = skeleton.bones.size();
    out.assign(n, Mat34::identity());

    std::uint32_t f0 = 0, f1 = 0;
    float t = 0.0f;
    if (anim && anim->frameCount > 0) {
        const float last = static_cast<float>(anim->frameCount - 1);
        const float f = std::clamp(frame, 0.0f, last);
        f0 = static_cast<std::uint32_t>(std::floor(f));
        f1 = std::min(f0 + 1, anim->frameCount - 1);
        t = f - static_cast<float>(f0);
    }
    auto channel3 = [&](auto&& get) { return lerp(get(f0), get(f1), t); };

    for (std::size_t i = 0; i < n; ++i) {
        const Skeleton::Bone& b = skeleton.bones[i];
        Mat34 local = Mat34::identity();
        if (anim && i < anim->boneCount())
            local = matrixFromEulersXZY(channel3([&](std::uint32_t fr) { return anim->boneRotation(fr, i); }));
        local.m3 = b.offset;
        if (anim && i == 0)
            local.m3 = channel3([&](std::uint32_t fr) { return anim->rootTranslation(fr); });
        // Parents precede children in the depth-first bone list.
        out[i] = (b.parent >= 0 && static_cast<std::size_t>(b.parent) < i)
                     ? Mat34::mul(local, out[static_cast<std::size_t>(b.parent)])
                     : local;
    }
}

// --- Mesh ----------------------------------------------------------------------------------

std::optional<PedMesh> parsePedMesh(std::string_view text, std::string* error) {
    struct RawMaterial {
        PedMesh::Material mtl;
        long long packets = -1;
        long long adjuncts = -1;
        long long primitives = -1;
    };
    struct Packet {
        std::vector<std::array<long long, 6>> adj;
        std::vector<std::array<long long, 3>> tri;
        std::vector<long long> mtx;
        long long declAdj = 0, declTri = 0, declMtx = 0;
    };

    PedMesh mesh;
    std::map<std::string, long long, std::less<>> header;
    std::vector<Vec3> verts, normals;
    std::vector<Vec4> colors;
    std::vector<Vec2> tex1;
    std::vector<RawMaterial> materials;
    std::vector<Packet> packets;
    std::vector<std::array<long long, 5>> flatAdj;
    std::vector<std::array<long long, 3>> flatTri;
    std::vector<long long> mtxv, mtxn;

    RawMaterial* mtl = nullptr;
    Packet* pk = nullptr;
    int lineNo = 0;
    auto bad = [&](std::string_view what) {
        fail(error, std::format("line {}: {}", lineNo, what));
        return std::nullopt;
    };

    for (std::string_view line : lines(text)) {
        ++lineNo;
        const auto t = tokens(line);
        if (t.empty())
            continue;
        const std::string_view k = t[0];

        if (mtl) {
            if (k == "}") {
                mtl = nullptr;
                continue;
            }
            float v[3];
            long long n = 0;
            if (k == "packets:" && t.size() >= 2 && toInt(t[1], n))
                mtl->packets = n;
            else if (k == "adjuncts:" && t.size() >= 2 && toInt(t[1], n))
                mtl->adjuncts = n;
            else if (k == "primitives:" && t.size() >= 2 && toInt(t[1], n))
                mtl->primitives = n;
            else if (k == "textures:" && t.size() >= 2 && toInt(t[1], n))
                mtl->mtl.textureCount = static_cast<int>(n);
            else if (k == "illum:" && t.size() >= 2)
                mtl->mtl.illum = std::string(t[1]);
            else if (k == "ambient:" && floats(t, 1, 3, v))
                mtl->mtl.ambient = {v[0], v[1], v[2]};
            else if (k == "diffuse:" && floats(t, 1, 3, v))
                mtl->mtl.diffuse = {v[0], v[1], v[2]};
            else if (k == "specular:" && floats(t, 1, 3, v))
                mtl->mtl.specular = {v[0], v[1], v[2]};
            else
                return bad(std::format("unexpected '{}' in material", k));
            continue;
        }
        if (pk) {
            if (k == "}") {
                if (static_cast<long long>(pk->adj.size()) != pk->declAdj ||
                    static_cast<long long>(pk->tri.size()) != pk->declTri ||
                    static_cast<long long>(pk->mtx.size()) != pk->declMtx)
                    return bad("packet contents do not match its header");
                pk = nullptr;
                continue;
            }
            if (k == "adj") {
                std::array<long long, 6> a{};
                if (!ints(t, 1, 6, a.data()))
                    return bad("bad adj");
                pk->adj.push_back(a);
            } else if (k == "tri") {
                std::array<long long, 3> a{};
                if (!ints(t, 1, 3, a.data()))
                    return bad("bad tri");
                pk->tri.push_back(a);
            } else if (k == "mtx") {
                for (std::size_t i = 1; i < t.size(); ++i) {
                    long long b = 0;
                    if (!toInt(t[i], b))
                        return bad("bad mtx");
                    pk->mtx.push_back(b);
                }
            } else {
                return bad(std::format("unexpected '{}' in packet", k));
            }
            continue;
        }

        if (k.ends_with(':') && t.size() >= 2 && k != "mtl") {
            if (k == "version:") {
                mesh.version = std::string(t[1]);
            } else {
                long long n = 0;
                if (!toInt(t[1], n) || n < 0)
                    return bad(std::format("bad header value for '{}'", k));
                header[std::string(k.substr(0, k.size() - 1))] = n;
            }
        } else if (k == "v" || k == "n") {
            float v[3];
            if (!floats(t, 1, 3, v))
                return bad("bad vector");
            (k == "v" ? verts : normals).push_back({v[0], v[1], v[2]});
        } else if (k == "c") {
            float v[4];
            if (!floats(t, 1, 4, v))
                return bad("bad colour");
            colors.push_back({v[0], v[1], v[2], v[3]});
        } else if (k == "t1") {
            float v[2];
            if (!floats(t, 1, 2, v))
                return bad("bad texture coordinate");
            tex1.push_back({v[0], v[1]});
        } else if (k == "t2") {
            // Second texture coordinate set; unused by the retail models.
        } else if (k == "mtl") {
            if (t.size() < 3 || t.back() != "{")
                return bad("expected 'mtl <name> {'");
            materials.emplace_back();
            mtl = &materials.back();
            mtl->mtl.name = std::string(t[1]);
        } else if (k == "packet") {
            long long h[3];
            if (!ints(t, 1, 3, h) || t.size() < 5 || t[4] != "{" || h[0] < 0 || h[1] < 0 || h[2] < 0)
                return bad("expected 'packet <adjuncts> <triangles> <matrices> {'");
            packets.emplace_back();
            pk = &packets.back();
            pk->declAdj = h[0];
            pk->declTri = h[1];
            pk->declMtx = h[2];
        } else if (k == "adj") {
            std::array<long long, 5> a{};
            if (!ints(t, 1, 5, a.data()))
                return bad("bad adj");
            flatAdj.push_back(a);
        } else if (k == "tri") {
            std::array<long long, 3> a{};
            if (!ints(t, 1, 3, a.data()))
                return bad("bad tri");
            flatTri.push_back(a);
        } else if (k == "mtxv" || k == "mtxn") {
            auto& dst = k == "mtxv" ? mtxv : mtxn;
            for (std::size_t i = 1; i < t.size(); ++i) {
                long long n = 0;
                if (!toInt(t[i], n) || n < 0)
                    return bad("bad matrix counts");
                dst.push_back(n);
            }
        } else {
            return bad(std::format("unexpected '{}'", k));
        }
    }
    if (mtl || pk)
        return bad("unterminated block");

    auto headerCount = [&](const char* key) { return header.contains(key) ? header[key] : -1; };
    if (mesh.version.empty())
        return bad("missing version");
    if (headerCount("verts") != static_cast<long long>(verts.size()) ||
        headerCount("normals") != static_cast<long long>(normals.size()) ||
        headerCount("colors") != static_cast<long long>(colors.size()) ||
        headerCount("tex1s") != static_cast<long long>(tex1.size()) ||
        headerCount("materials") != static_cast<long long>(materials.size()))
        return bad("pool sizes do not match the header");
    const long long boneCount = headerCount("matrices");
    if (boneCount <= 0 || boneCount > 255)
        return bad("bad matrix count");
    mesh.boneCount = static_cast<std::uint32_t>(boneCount);

    // Builds one output vertex from pool indices.
    auto makeVertex = [&](long long v, long long n, long long c, long long tx, long long bone,
                          PedMesh::Vertex& out) {
        if (v < 0 || v >= static_cast<long long>(verts.size()) || n < 0 ||
            n >= static_cast<long long>(normals.size()) || bone < 0 || bone >= boneCount)
            return false;
        out.position = verts[static_cast<std::size_t>(v)];
        out.normal = normals[static_cast<std::size_t>(n)];
        if (c >= 0 && c < static_cast<long long>(colors.size()))
            out.color = colors[static_cast<std::size_t>(c)];
        else if (!colors.empty())
            return false;
        if (tx >= 0 && tx < static_cast<long long>(tex1.size()))
            out.uv = tex1[static_cast<std::size_t>(tx)];
        else if (!tex1.empty())
            return false;
        out.bone = static_cast<std::uint32_t>(bone);
        return true;
    };

    const bool packetLayout = !packets.empty();
    if (packetLayout) {
        if (!flatAdj.empty() || !flatTri.empty())
            return bad("mixed packet and flat geometry");
        std::size_t nextPacket = 0;
        for (auto& m : materials) {
            if (m.packets < 0)
                return bad(std::format("material {} has no packet count", m.mtl.name));
            m.mtl.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
            for (long long p = 0; p < m.packets; ++p) {
                if (nextPacket >= packets.size())
                    return bad("materials reference more packets than present");
                const Packet& pkt = packets[nextPacket++];
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (const auto& a : pkt.adj) {
                    const long long slot = a[5];
                    if (slot < 0 || slot >= static_cast<long long>(pkt.mtx.size()))
                        return bad("adjunct matrix slot out of range");
                    PedMesh::Vertex vx;
                    if (!makeVertex(a[0], a[1], a[2], a[3], pkt.mtx[static_cast<std::size_t>(slot)], vx))
                        return bad("adjunct index out of range");
                    mesh.vertices.push_back(vx);
                }
                for (const auto& tr : pkt.tri)
                    for (long long i : tr) {
                        if (i < 0 || i >= static_cast<long long>(pkt.adj.size()))
                            return bad("triangle index out of range");
                        mesh.indices.push_back(base + static_cast<std::uint32_t>(i));
                    }
            }
            m.mtl.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - m.mtl.firstIndex;
            if (m.primitives >= 0 && m.primitives * 3 != static_cast<long long>(m.mtl.indexCount))
                return bad(std::format("material {} primitive count mismatch", m.mtl.name));
        }
        if (nextPacket != packets.size())
            return bad("packets not owned by any material");
    } else {
        // Flat layout: vertex pool sorted by bone, counts in mtxv.
        if (static_cast<long long>(mtxv.size()) != boneCount)
            return bad("missing or short mtxv");
        std::vector<std::uint32_t> vertexBone;
        for (std::size_t b = 0; b < mtxv.size(); ++b)
            vertexBone.insert(vertexBone.end(), static_cast<std::size_t>(mtxv[b]), static_cast<std::uint32_t>(b));
        if (vertexBone.size() != verts.size())
            return bad("mtxv does not cover the vertex pool");
        for (const auto& a : flatAdj) {
            if (a[0] < 0 || a[0] >= static_cast<long long>(verts.size()))
                return bad("adjunct vertex out of range");
            PedMesh::Vertex vx;
            if (!makeVertex(a[0], a[1], a[2], a[3], vertexBone[static_cast<std::size_t>(a[0])], vx))
                return bad("adjunct index out of range");
            mesh.vertices.push_back(vx);
        }
        std::size_t nextTri = 0;
        for (auto& m : materials) {
            if (m.primitives < 0)
                return bad(std::format("material {} has no primitive count", m.mtl.name));
            m.mtl.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
            for (long long p = 0; p < m.primitives; ++p) {
                if (nextTri >= flatTri.size())
                    return bad("materials reference more triangles than present");
                for (long long i : flatTri[nextTri++]) {
                    if (i < 0 || i >= static_cast<long long>(mesh.vertices.size()))
                        return bad("triangle index out of range");
                    mesh.indices.push_back(static_cast<std::uint32_t>(i));
                }
            }
            m.mtl.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - m.mtl.firstIndex;
        }
        if (nextTri != flatTri.size())
            return bad("triangles not owned by any material");
    }

    // In the packet layout a material split over several packets repeats the
    // adjuncts its packets share, so only the flat layout's adjunct total is
    // exact (the header counts the unsplit pool, one entry per normal).
    if ((!packetLayout && headerCount("adjuncts") != static_cast<long long>(mesh.vertices.size())) ||
        headerCount("primitives") * 3 != static_cast<long long>(mesh.indices.size()))
        return bad("adjunct/primitive totals do not match the header");
    for (auto& m : materials)
        mesh.materials.push_back(std::move(m.mtl));
    return mesh;
}

// --- Shaders --------------------------------------------------------------------------------

const PedShader* PedShaderSet::get(std::uint32_t variant, std::uint32_t material) const {
    if (variant >= variantCount || material >= materialCount)
        return nullptr;
    return &shaders[static_cast<std::size_t>(variant) * materialCount + material];
}

std::optional<PedShaderSet> parsePedShaders(std::span<const std::byte> data, std::string* error) {
    detail::Reader r(data);
    PedShaderSet set;
    set.variantCount = r.u32();
    set.materialCount = r.u32();
    if (!r.ok() || set.variantCount == 0 || set.materialCount == 0 || set.variantCount > 4096 ||
        set.materialCount > 4096) {
        fail(error, "bad shader header");
        return std::nullopt;
    }
    const std::size_t total = static_cast<std::size_t>(set.variantCount) * set.materialCount;
    if (total * 69 > r.remaining()) { // each entry is at least 1 + 17 * 4 bytes
        fail(error, "shader table larger than the file");
        return std::nullopt;
    }
    set.shaders.reserve(total);
    for (std::size_t i = 0; i < total; ++i) {
        PedShader s;
        const std::uint8_t len = r.u8();
        for (std::uint8_t c = 0; c < len; ++c)
            s.texture.push_back(static_cast<char>(r.u8()));
        s.diffuse = r.vec4();
        s.ambient = r.vec4();
        s.specular = r.vec4();
        s.emissive = r.vec4();
        s.power = r.f32();
        if (!r.ok()) {
            fail(error, "truncated shader table");
            return std::nullopt;
        }
        set.shaders.push_back(std::move(s));
    }
    if (r.remaining() != 0) {
        fail(error, std::format("{} trailing bytes after the shader table", r.remaining()));
        return std::nullopt;
    }
    return set;
}

// --- Rays / remap ---------------------------------------------------------------------------

std::optional<PedRays> parsePedRays(std::string_view text, std::string* error) {
    const auto ls = lines(text);
    std::size_t i = 0;
    auto nextNonEmpty = [&]() -> std::vector<std::string_view> {
        while (i < ls.size()) {
            auto t = tokens(ls[i++]);
            if (!t.empty())
                return t;
        }
        return {};
    };
    long long count = 0;
    auto head = nextNonEmpty();
    if (head.size() != 1 || !toInt(head[0], count) || count <= 0 || count > 255) {
        fail(error, "bad bone count");
        return std::nullopt;
    }
    PedRays rays;
    for (long long b = 0; b < count; ++b) {
        auto t = nextNonEmpty();
        float v[3];
        long long n[2];
        if (t.size() != 5 || !floats(t, 0, 3, v) || !ints(t, 3, 2, n)) {
            fail(error, std::format("bad bone row {}", b));
            return std::nullopt;
        }
        rays.bones.push_back({{v[0], v[1], v[2]}, static_cast<int>(n[0]), static_cast<int>(n[1])});
    }
    while (true) {
        auto t = nextNonEmpty();
        if (t.empty())
            break;
        if (static_cast<long long>(t.size()) != count) {
            fail(error, "variant row with the wrong number of values");
            return std::nullopt;
        }
        std::vector<int> row;
        for (auto tok : t) {
            long long n = 0;
            if (!toInt(tok, n)) {
                fail(error, "bad variant value");
                return std::nullopt;
            }
            row.push_back(static_cast<int>(n));
        }
        rays.variants.push_back(std::move(row));
    }
    return rays;
}

std::optional<std::vector<int>> parsePedRemap(std::string_view text, std::string* error) {
    std::vector<long long> all;
    for (auto line : lines(text))
        for (auto t : tokens(line)) {
            long long n = 0;
            if (!toInt(t, n)) {
                fail(error, std::format("bad value '{}'", t));
                return std::nullopt;
            }
            all.push_back(n);
        }
    if (all.empty() || all[0] < 0 || static_cast<std::size_t>(all[0]) != all.size() - 1) {
        fail(error, "count does not match the number of values");
        return std::nullopt;
    }
    return std::vector<int>(all.begin() + 1, all.end());
}

// --- Animation table ----------------------------------------------------------------------------

const PedAnimState* PedAnimTable::find(std::string_view name) const {
    for (const auto& s : states)
        if (str::iequals(s.name, name))
            return &s;
    return nullptr;
}

std::optional<PedAnimTable> parsePedAnimTable(std::string_view text, std::string* error) {
    PedAnimTable table;
    int lineNo = 0;
    for (auto line : lines(text)) {
        ++lineNo;
        const auto trimmed = str::trim(line);
        if (trimmed.empty() || trimmed.starts_with('#'))
            continue;
        auto cells = str::split(trimmed, ',');
        if (cells.size() < 9) {
            fail(error, std::format("line {}: expected 9 columns", lineNo));
            return std::nullopt;
        }
        PedAnimState s;
        s.name = std::string(str::trim(cells[0]));
        s.animFile = stem(cells[1]);
        long long first = 0, last = 0;
        if (s.name.empty() || s.animFile.empty() || !toInt(str::trim(cells[2]), first) ||
            !toInt(str::trim(cells[3]), last) || !toFloat(str::trim(cells[4]), s.forwardOffset) ||
            !toFloat(str::trim(cells[5]), s.forwardDistance) || !toFloat(str::trim(cells[6]), s.sideOffset) ||
            !toFloat(str::trim(cells[7]), s.sideDistance) || first < 1 || last < first) {
            fail(error, std::format("line {}: bad values", lineNo));
            return std::nullopt;
        }
        s.firstFrame = static_cast<int>(first);
        s.lastFrame = static_cast<int>(last);
        s.next = std::string(str::trim(cells[8]));
        table.states.push_back(std::move(s));
    }
    if (table.states.empty()) {
        fail(error, "no animation states");
        return std::nullopt;
    }
    return table;
}

// --- Whole type ----------------------------------------------------------------------------------

const PedAnimation* PedType::animation(std::string_view stateOrFile) const {
    std::string key = stem(stateOrFile);
    if (const PedAnimState* s = table.find(stateOrFile))
        key = s->animFile;
    const auto it = animations.find(key);
    return it == animations.end() ? nullptr : &it->second;
}

std::optional<PedType> loadPedType(std::string_view name, const ReadFileFn& read, std::string* error) {
    PedType type;
    type.name = str::lower(name);
    const std::string base = "anim/" + type.name;
    std::string err;

    auto required = [&](const std::string& path) -> std::optional<std::vector<std::byte>> {
        auto bytes = read(path);
        if (!bytes)
            fail(error, std::format("{} not found", path));
        return bytes;
    };

    auto skel = required(base + ".skel");
    if (!skel)
        return std::nullopt;
    auto s = parseSkeleton(asText(*skel), &err);
    if (!s) {
        fail(error, std::format("{}.skel: {}", base, err));
        return std::nullopt;
    }
    type.skeleton = std::move(*s);

    auto mod = required(base + ".mod");
    if (!mod)
        return std::nullopt;
    auto m = parsePedMesh(asText(*mod), &err);
    if (!m) {
        fail(error, std::format("{}.mod: {}", base, err));
        return std::nullopt;
    }
    type.mesh = std::move(*m);
    if (type.mesh.boneCount != type.skeleton.bones.size()) {
        fail(error, std::format("{}: mesh has {} matrices but the skeleton {} bones", base, type.mesh.boneCount,
                                type.skeleton.bones.size()));
        return std::nullopt;
    }

    auto sh = required(base + ".shaders");
    if (!sh)
        return std::nullopt;
    auto shaders = parsePedShaders(*sh, &err);
    if (!shaders) {
        fail(error, std::format("{}.shaders: {}", base, err));
        return std::nullopt;
    }
    if (shaders->materialCount != type.mesh.materials.size()) {
        fail(error, std::format("{}: {} shader materials for {} mesh materials", base, shaders->materialCount,
                                type.mesh.materials.size()));
        return std::nullopt;
    }
    type.shaders = std::move(*shaders);

    if (auto rays = read(base + ".rays")) {
        type.rays = parsePedRays(asText(*rays), &err);
        if (!type.rays)
            log::warn("ped: {}.rays: {}", base, err);
    }
    if (auto remap = read(base + ".remap")) {
        if (auto v = parsePedRemap(asText(*remap), &err))
            type.remap = std::move(*v);
        else
            log::warn("ped: {}.remap: {}", base, err);
    }

    auto csv = required(base + ".csv");
    if (!csv)
        return std::nullopt;
    auto table = parsePedAnimTable(asText(*csv), &err);
    if (!table) {
        fail(error, std::format("{}.csv: {}", base, err));
        return std::nullopt;
    }
    type.table = std::move(*table);

    for (const auto& state : type.table.states) {
        if (type.animations.contains(state.animFile))
            continue;
        const std::string path = "anim/" + state.animFile + ".anim";
        auto bytes = read(path);
        if (!bytes) {
            log::warn("ped: {} (state {}) not found", path, state.name);
            continue;
        }
        auto anim = parsePedAnimation(*bytes, &err);
        if (!anim) {
            log::warn("ped: {}: {}", path, err);
            continue;
        }
        if (anim->boneCount() != type.skeleton.bones.size()) {
            log::warn("ped: {} animates {} bones, skeleton has {}", path, anim->boneCount(),
                      type.skeleton.bones.size());
            continue;
        }
        type.animations.emplace(state.animFile, std::move(*anim));
    }
    return type;
}

std::vector<std::string> findPedTypes(const std::vector<std::string>& paths) {
    std::vector<std::string> out;
    for (const auto& p : paths) {
        const std::string l = str::lower(p);
        if (l.starts_with("anim/") && l.ends_with(".mod") && l.find('/', 5) == std::string::npos)
            out.push_back(l.substr(5, l.size() - 9));
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool isKnownBrokenPedAsset(std::string_view path) {
    return str::iequals(path, "anim/pedanim_manantrnch.anim");
}

} // namespace mm2::asset
