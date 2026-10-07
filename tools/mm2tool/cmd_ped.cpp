// Pedestrian commands: pedinfo, ped2obj, pedpose, pedcheck.
#include "Command.h"
#include "Common.h"

#include "asset/Image.h"
#include "asset/Ped.h"
#include "core/File.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <print>

namespace mm2::tool {
namespace {

asset::ReadFileFn reader(const vfs::FileSystem& fs) {
    return [&fs](std::string_view path) -> std::optional<std::vector<std::byte>> {
        auto f = fs.open(path);
        if (!f)
            return std::nullopt;
        auto data = f->readAll();
        if (data.size() != f->size())
            return std::nullopt;
        return data;
    };
}

std::optional<asset::PedType> loadType(const vfs::FileSystem& fs, std::string_view name) {
    std::string err;
    std::string type(name);
    if (!str::istartsWith(type, "pedmodel_"))
        type = "pedmodel_" + type;
    auto t = asset::loadPedType(type, reader(fs), &err);
    if (!t)
        std::println(stderr, "error: {}", err);
    return t;
}

std::vector<std::string> allPaths(const vfs::FileSystem& fs) {
    std::vector<std::string> paths;
    fs.forEachFile([&](const vfs::EntryInfo& e) { paths.push_back(e.path); });
    return paths;
}

// Model-space vertex positions and normals for a pose.
void skin(const asset::PedType& t, const std::vector<Mat34>& bones, std::vector<Vec3>& pos, std::vector<Vec3>& nrm) {
    pos.clear();
    nrm.clear();
    for (const auto& v : t.mesh.vertices) {
        const Mat34& m = bones[v.bone];
        pos.push_back(m.transform(v.position));
        nrm.push_back(m.transformDir(v.normal).normalized());
    }
}

int cmdPedInfo(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    auto t = loadType(*fs, args[1]);
    if (!t)
        return 1;
    std::println("{}: {} bones, mesh {} ({} vertices, {} triangles, {} materials), {} shader variants",
                 t->name, t->skeleton.bones.size(), t->mesh.version, t->mesh.vertices.size(),
                 t->mesh.indices.size() / 3, t->mesh.materials.size(), t->shaders.variantCount);
    for (std::size_t i = 0; i < t->skeleton.bones.size(); ++i) {
        const auto& b = t->skeleton.bones[i];
        std::println("  bone {:2} {:12} parent {:2} offset ({:.3f}, {:.3f}, {:.3f})", i, b.name, b.parent,
                     b.offset.x, b.offset.y, b.offset.z);
    }
    for (const auto& m : t->mesh.materials)
        std::println("  material {:24} {:4} triangles diffuse ({:.2f}, {:.2f}, {:.2f})", m.name, m.indexCount / 3,
                     m.diffuse.x, m.diffuse.y, m.diffuse.z);
    for (const auto& s : t->table.states) {
        const auto* a = t->animation(s.animFile);
        std::string motion;
        if (a) {
            const Vec3 d = a->rootTranslation(a->frameCount - 1) - a->rootTranslation(0);
            motion = std::format("{:3} frames, root moves fwd {:.3f} left {:.3f}, cycle {:.3f}", a->frameCount,
                                 -d.z, -d.x, a->cycleDistance);
        } else {
            motion = "MISSING";
        }
        std::println("  state {:16} {:20} frames {:2}-{:2} fwd {:.3f}+{:.3f} side {:.3f}+{:.3f} -> {:14} [{}]", s.name,
                     s.animFile, s.firstFrame, s.lastFrame, s.forwardOffset, s.forwardDistance, s.sideOffset,
                     s.sideDistance, s.next, motion);
    }
    if (t->rays)
        std::println("  rays: {} bone rows, {} variant rows", t->rays->bones.size(), t->rays->variants.size());
    if (!t->remap.empty())
        std::println("  remap: {} entries", t->remap.size());
    return 0;
}

int cmdPed2Obj(std::span<char* const> args) {
    if (args.size() < 5)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    auto t = loadType(*fs, args[1]);
    if (!t)
        return 1;
    const std::string animName = args[2];
    const asset::PedAnimation* anim = animName == "bind" ? nullptr : t->animation(animName);
    if (!anim && animName != "bind") {
        std::println(stderr, "error: no animation '{}' (use a state name, an anim file or 'bind')", animName);
        return 1;
    }
    const float frame = static_cast<float>(std::atof(args[3]));
    const int variant = args.size() > 5 ? std::atoi(args[5]) : -1;
    std::vector<Mat34> bones;
    asset::posePed(t->skeleton, anim, frame, bones);
    std::vector<Vec3> pos, nrm;
    skin(*t, bones, pos, nrm);

    const auto objPath = str::toPath(args[4]);
    auto mtlPath = objPath;
    mtlPath.replace_extension(".mtl");
    std::string obj = std::format("# {} {} frame {}\nmtllib {}\n", t->name, animName, frame,
                                  str::fromPath(mtlPath.filename()));
    std::string mtl;
    for (std::size_t i = 0; i < pos.size(); ++i)
        obj += std::format("v {} {} {}\nvn {} {} {}\n", pos[i].x, pos[i].y, pos[i].z, nrm[i].x, nrm[i].y, nrm[i].z);
    for (std::size_t mi = 0; mi < t->mesh.materials.size(); ++mi) {
        const auto& m = t->mesh.materials[mi];
        Vec3 d = m.diffuse;
        if (const auto* s = variant >= 0 ? t->shaders.get(static_cast<std::uint32_t>(variant),
                                                          static_cast<std::uint32_t>(mi))
                                         : nullptr)
            d = s->diffuse.xyz();
        std::string name = m.name;
        std::ranges::replace(name, ':', '_');
        mtl += std::format("newmtl {}\nKd {} {} {}\n\n", name, d.x, d.y, d.z);
        obj += std::format("usemtl {}\n", name);
        for (std::uint32_t i = 0; i < m.indexCount; i += 3) {
            const auto a = t->mesh.indices[m.firstIndex + i] + 1, b = t->mesh.indices[m.firstIndex + i + 1] + 1,
                       c = t->mesh.indices[m.firstIndex + i + 2] + 1;
            obj += std::format("f {}//{} {}//{} {}//{}\n", a, a, b, b, c, c);
        }
    }
    if (!file::writeAtomic(objPath, obj) || !file::writeAtomic(mtlPath, mtl)) {
        std::println(stderr, "error: cannot write {}", args[4]);
        return 1;
    }
    std::println("wrote {} ({} vertices)", args[4], pos.size());
    return 0;
}

// Minimal z-buffered flat-shaded rasterizer for contact sheets.
struct Canvas {
    int w, h;
    std::vector<std::uint8_t> rgba;
    std::vector<float> depth;
    Canvas(int w_, int h_) : w(w_), h(h_), rgba(static_cast<std::size_t>(w_) * h_ * 4), depth(static_cast<std::size_t>(w_) * h_, 1e30f) {}

    void fill(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i] = r;
            rgba[i + 1] = g;
            rgba[i + 2] = b;
            rgba[i + 3] = 255;
        }
    }
    void put(int x, int y, Vec3 c) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        auto* p = &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
        p[0] = static_cast<std::uint8_t>(std::clamp(c.x, 0.0f, 1.0f) * 255);
        p[1] = static_cast<std::uint8_t>(std::clamp(c.y, 0.0f, 1.0f) * 255);
        p[2] = static_cast<std::uint8_t>(std::clamp(c.z, 0.0f, 1.0f) * 255);
    }
    // Screen-space triangle with per-triangle colour; z smaller = nearer.
    void triangle(Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-6f)
            return;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float w0 = (b.x - px) * (c.y - py) - (b.y - py) * (c.x - px);
                float w1 = (c.x - px) * (a.y - py) - (c.y - py) * (a.x - px);
                float w2 = (a.x - px) * (b.y - py) - (a.y - py) * (b.x - px);
                if (area < 0) {
                    w0 = -w0;
                    w1 = -w1;
                    w2 = -w2;
                }
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                const float s = std::abs(area);
                const float z = (w0 * a.z + w1 * b.z + w2 * c.z) / s;
                float& d = depth[static_cast<std::size_t>(y) * w + x];
                if (z < d) {
                    d = z;
                    put(x, y, color);
                }
            }
    }
    void line(Vec3 a, Vec3 b, Vec3 color) {
        const int steps = static_cast<int>(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y))) + 1;
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            put(static_cast<int>(a.x + (b.x - a.x) * t), static_cast<int>(a.y + (b.y - a.y) * t), color);
        }
    }
};

int cmdPedPose(std::span<char* const> args) {
    if (args.size() < 4)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    auto t = loadType(*fs, args[1]);
    if (!t)
        return 1;
    const std::string animName = args[2];
    const asset::PedAnimation* anim = animName == "bind" ? nullptr : t->animation(animName);
    if (!anim && animName != "bind") {
        std::println(stderr, "error: no animation '{}'", animName);
        return 1;
    }
    int tiles = args.size() > 4 ? std::max(1, std::atoi(args[4])) : 8;
    const int variant = args.size() > 5 ? std::atoi(args[5]) : -1;
    const int frames = anim ? static_cast<int>(anim->frameCount) : 1;
    tiles = std::min(tiles, frames);

    constexpr int kTileW = 150, kTileH = 260;
    constexpr float kScale = 110.0f; // pixels per metre
    // Rows: side view (camera on +X, forward -Z to the right), front view
    // (camera ahead of the ped on -Z looking back), top view (camera above).
    constexpr int kRows = 3;
    Canvas canvas(kTileW * tiles, kTileH * kRows);
    canvas.fill(235, 235, 230);
    const Vec3 light = Vec3{0.4f, 0.8f, -0.45f}.normalized();

    std::vector<Mat34> bones;
    std::vector<Vec3> pos, nrm;
    for (int ti = 0; ti < tiles; ++ti) {
        const float frame =
            tiles > 1 ? static_cast<float>(ti) * static_cast<float>(frames - 1) / static_cast<float>(tiles - 1) : 0.0f;
        asset::posePed(t->skeleton, anim, frame, bones);
        skin(*t, bones, pos, nrm);
        // Keep the figure in its tile: centre on the root's horizontal position.
        const Vec3 root = bones[0].m3;
        for (int row = 0; row < kRows; ++row) {
            const float ox = static_cast<float>(ti * kTileW + kTileW / 2);
            const float oy = static_cast<float>(row * kTileH + kTileH - 20);
            auto project = [&](Vec3 p) -> Vec3 {
                p = p - Vec3{root.x, 0, root.z};
                if (row == 0)
                    return {ox - p.z * kScale, oy - p.y * kScale, -p.x};
                if (row == 1)
                    return {ox - p.x * kScale, oy - p.y * kScale, p.z};
                return {ox + p.x * kScale, oy - kTileH * 0.45f + p.z * kScale, -p.y}; // top: forward = up
            };
            // Ground line and tile border.
            canvas.line({ox - kTileW / 2.0f, oy, 0}, {ox + kTileW / 2.0f - 1, oy, 0}, {0.6f, 0.6f, 0.6f});
            canvas.line({ox + kTileW / 2.0f - 1, oy - kTileH + 20, 0}, {ox + kTileW / 2.0f - 1, oy + 19, 0},
                        {0.8f, 0.8f, 0.8f});
            for (std::size_t mi = 0; mi < t->mesh.materials.size(); ++mi) {
                const auto& m = t->mesh.materials[mi];
                Vec3 base = m.diffuse;
                if (const auto* s = variant >= 0 ? t->shaders.get(static_cast<std::uint32_t>(variant),
                                                                  static_cast<std::uint32_t>(mi))
                                                 : nullptr)
                    base = s->diffuse.xyz();
                for (std::uint32_t i = 0; i < m.indexCount; i += 3) {
                    const auto ia = t->mesh.indices[m.firstIndex + i], ib = t->mesh.indices[m.firstIndex + i + 1],
                               ic = t->mesh.indices[m.firstIndex + i + 2];
                    const Vec3 n = (nrm[ia] + nrm[ib] + nrm[ic]).normalized();
                    const float lit = 0.35f + 0.65f * std::max(0.0f, n.dot(light));
                    // Keep dark clothing visible against the background.
                    const Vec3 c = Vec3{std::max(base.x, 0.12f), std::max(base.y, 0.12f), std::max(base.z, 0.12f)} * lit;
                    canvas.triangle(project(pos[ia]), project(pos[ib]), project(pos[ic]), c);
                }
            }
            // Skeleton overlay.
            for (std::size_t b = 1; b < bones.size(); ++b) {
                const int parent = t->skeleton.bones[b].parent;
                if (parent < 0)
                    continue;
                const Vec3 a = project(bones[static_cast<std::size_t>(parent)].m3);
                const Vec3 c = project(bones[b].m3);
                canvas.line({a.x, a.y, 0}, {c.x, c.y, 0}, {0.9f, 0.1f, 0.1f});
            }
        }
    }

    asset::Image::Level level;
    level.width = static_cast<std::uint32_t>(canvas.w);
    level.height = static_cast<std::uint32_t>(canvas.h);
    level.rgba.resize(canvas.rgba.size());
    // encodePng expects rows bottom-to-top (game convention).
    for (int y = 0; y < canvas.h; ++y)
        std::copy_n(&canvas.rgba[static_cast<std::size_t>(y) * canvas.w * 4], canvas.w * 4,
                    &level.rgba[static_cast<std::size_t>(canvas.h - 1 - y) * canvas.w * 4]);
    const auto png = asset::encodePng(level);
    if (png.empty() || !file::writeAtomic(str::toPath(args[3]), png)) {
        std::println(stderr, "error: cannot write {}", args[3]);
        return 1;
    }
    std::println("wrote {} ({} frames of {})", args[3], tiles, frames);
    return 0;
}

int cmdPedCheck(std::span<char* const> args) {
    if (args.empty())
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    const auto read = reader(*fs);
    int ok = 0, failed = 0, broken = 0;
    for (const auto& path : allPaths(*fs)) {
        if (!path.starts_with("anim/"))
            continue;
        const std::string ext = path.substr(path.rfind('.') + 1);
        auto bytes = read(path);
        if (!bytes)
            continue;
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        std::string err = "unrecognised";
        bool good = true;
        if (ext == "anim")
            good = asset::parsePedAnimation(*bytes, &err).has_value();
        else if (ext == "skel")
            good = asset::parseSkeleton(text, &err).has_value();
        else if (ext == "mod")
            good = asset::parsePedMesh(text, &err).has_value();
        else if (ext == "shaders")
            good = asset::parsePedShaders(*bytes, &err).has_value();
        else if (ext == "rays")
            good = asset::parsePedRays(text, &err).has_value();
        else if (ext == "remap")
            good = asset::parsePedRemap(text, &err).has_value();
        else if (ext == "csv")
            good = asset::parsePedAnimTable(text, &err).has_value();
        else
            continue; // CVS metadata, batch files
        if (good) {
            ++ok;
        } else if (asset::isKnownBrokenPedAsset(path)) {
            ++broken;
        } else {
            ++failed;
            std::println("FAILED {}: {}", path, err);
        }
    }
    int types = 0;
    for (const auto& type : asset::findPedTypes(allPaths(*fs))) {
        std::string err;
        auto t = asset::loadPedType(type, read, &err);
        if (!t) {
            ++failed;
            std::println("FAILED type {}: {}", type, err);
            continue;
        }
        ++types;
        std::println("type {:18} {:3} vertices {:3} triangles {:2} variants {:2} animations", type,
                     t->mesh.vertices.size(), t->mesh.indices.size() / 3, t->shaders.variantCount,
                     t->animations.size());
    }
    std::println("{} files ok, {} failed, {} known-broken; {} pedestrian types", ok, failed, broken, types);
    return failed ? 1 : 0;
}

const Registrar r1({"pedinfo", "<container> <type>", "describe a pedestrian type (e.g. man, pedmodel_woman)",
                    &cmdPedInfo});
const Registrar r2({"ped2obj", "<container> <type> <state|anim|bind> <frame> <out.obj> [variant]",
                    "export a posed pedestrian mesh", &cmdPed2Obj});
const Registrar r3({"pedpose", "<container> <type> <state|anim|bind> <out.png> [tiles] [variant]",
                    "render a contact sheet (side/front/top views) of an animation", &cmdPedPose});
const Registrar r4({"pedcheck", "<container>", "parse every pedestrian file and type", &cmdPedCheck});

} // namespace
} // namespace mm2::tool
