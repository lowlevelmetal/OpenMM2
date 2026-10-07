// Model, texture and bound commands: texinfo, tex2png, pkginfo, pkg2obj,
// bndinfo, bnd2obj, assetcheck.
#include "Command.h"
#include "Common.h"

#include "asset/Bound.h"
#include "asset/Image.h"
#include "asset/Mtx.h"
#include "asset/Pkg.h"
#include "asset/VehicleModel.h"
#include "core/File.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <map>
#include <print>
#include <set>

namespace mm2::tool {
namespace {

std::vector<vfs::EntryInfo> matching(const vfs::FileSystem& fs, std::string_view pattern) {
    std::vector<vfs::EntryInfo> files;
    fs.forEachFile([&](const vfs::EntryInfo& e) {
        if (pattern.empty() || globMatch(pattern, e.path))
            files.push_back(e);
    });
    std::ranges::sort(files, {}, &vfs::EntryInfo::path);
    return files;
}

std::string_view formatName(asset::TexFormat f) {
    switch (f) {
    case asset::TexFormat::P8: return "P8";
    case asset::TexFormat::PA8: return "PA8";
    case asset::TexFormat::P4: return "P4";
    case asset::TexFormat::PA4: return "PA4";
    case asset::TexFormat::RGB888: return "RGB888";
    case asset::TexFormat::RGBA8888: return "RGBA8888";
    }
    return "?";
}

std::string baseName(std::string_view path) {
    auto slash = path.rfind('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    auto dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

// Texture file for a material texture name, trying the formats the game uses.
std::optional<std::string> findTexture(const vfs::FileSystem& fs, std::string_view name) {
    for (const char* ext : {".tex", ".tga", ".bmp", ".jpg"}) {
        std::string p = std::format("texture/{}{}", str::lower(name), ext);
        if (fs.exists(p))
            return p;
    }
    return std::nullopt;
}

int cmdTexInfo(std::span<char* const> args) {
    if (args.empty())
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    for (const auto& e : matching(*fs, args.size() > 1 ? args[1] : "*.tex")) {
        if (!str::iendsWith(e.path, ".tex"))
            continue;
        auto data = readFile(*fs, e.path);
        if (!data)
            continue;
        std::string err;
        auto h = asset::parseTexHeader(*data, &err);
        if (!h) {
            std::println("{}: {}", e.path, err);
            continue;
        }
        std::println("{:4}x{:<4} {:8} mips={:<2} flags=0x{:05x}  {}", h->width, h->height, formatName(h->format),
                     h->mipCount, h->flags, e.path);
    }
    return 0;
}

int cmdTex2Png(std::span<char* const> args) {
    if (args.size() < 3)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    const bool allMips = args.size() > 3 && std::string_view(args[3]) == "--mips";
    const auto outDir = str::toPath(args[2]);
    int count = 0, failures = 0;
    for (const auto& e : matching(*fs, args[1])) {
        if (!str::iendsWith(e.path, ".tex") && !str::iendsWith(e.path, ".tga") && !str::iendsWith(e.path, ".jpg") &&
            !str::iendsWith(e.path, ".bmp"))
            continue;
        auto data = readFile(*fs, e.path);
        std::string err;
        auto img = data ? asset::decodeImageFile(e.path, *data, &err) : std::nullopt;
        if (!img) {
            std::println(stderr, "{}: {}", e.path, err);
            ++failures;
            continue;
        }
        const std::size_t levels = allMips ? img->levels.size() : 1;
        for (std::size_t l = 0; l < levels; ++l) {
            const std::string name =
                l == 0 ? baseName(e.path) + ".png" : std::format("{}.mip{}.png", baseName(e.path), l);
            if (!file::writeAtomic(outDir / name, asset::encodePng(img->levels[l]))) {
                std::println(stderr, "cannot write {}", name);
                ++failures;
            }
        }
        ++count;
    }
    std::println("converted {} images to {}", count, args[2]);
    return failures ? 1 : 0;
}

std::string_view lodName(asset::Lod l) {
    return l == asset::Lod::None ? std::string_view("-") : asset::lodSuffix(l);
}

int cmdPkgInfo(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    auto data = readFile(*fs, args[1]);
    if (!data)
        return 1;
    std::string err;
    auto pkg = asset::parsePkg(*data, &err);
    if (!pkg) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    std::println("PKG{} with {} meshes, {} paint jobs x {} materials ({})", pkg->version, pkg->meshes.size(),
                 pkg->paintjobs.size(), pkg->shadersPerPaintjob,
                 (pkg->shaderType & 0x80) ? "compact byte colours" : "float colours");
    for (const auto& m : pkg->meshes) {
        const auto b = m.bounds();
        std::println("  {:20} part={:16} lod={:2} fvf=0x{:03x} sections={:<3} verts={:<5} tris={:<5} "
                     "bounds=({:.3f},{:.3f},{:.3f})..({:.3f},{:.3f},{:.3f})",
                     m.name, m.part, lodName(m.lod), m.fvf, m.sections.size(), m.vertexCount(), m.triangleCount(),
                     b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
    }
    for (std::size_t p = 0; p < pkg->paintjobs.size(); ++p) {
        std::println("  paint job {}:", p);
        for (std::size_t i = 0; i < pkg->paintjobs[p].size(); ++i) {
            const auto& mt = pkg->paintjobs[p][i];
            std::println("    [{:2}] {:28} diffuse=({:.2f},{:.2f},{:.2f},{:.2f}) spec=({:.2f},{:.2f},{:.2f}) "
                         "power={:.2f}",
                         i, mt.texture.empty() ? "(none)" : mt.texture, mt.diffuse.x, mt.diffuse.y, mt.diffuse.z,
                         mt.diffuse.w, mt.specular.x, mt.specular.y, mt.specular.z, mt.shininess);
        }
    }
    if (pkg->offset)
        std::println("  offset ({:.3f}, {:.3f}, {:.3f})", pkg->offset->x, pkg->offset->y, pkg->offset->z);
    for (const auto& x : pkg->xrefs)
        std::println("  xref {:24} at ({:.3f}, {:.3f}, {:.3f})", x.name, x.transform.m3.x, x.transform.m3.y,
                     x.transform.m3.z);
    for (const auto& w : pkg->warnings)
        std::println("  warning: {}", w);

    // Pivots, when this is a multi-part model.
    const std::string base = baseName(args[1]);
    auto vm = asset::loadVehicleModel(base, [&](std::string_view p) -> std::optional<std::vector<std::byte>> {
        auto f = fs->open(p);
        if (!f)
            return std::nullopt;
        return f->readAll();
    });
    if (vm) {
        for (const auto& [name, mtx] : vm->pivots)
            std::println("  pivot {:16} origin=({:.3f}, {:.3f}, {:.3f}) half=({:.3f}, {:.3f}, {:.3f})", name,
                         mtx.origin.x, mtx.origin.y, mtx.origin.z, mtx.halfExtent().x, mtx.halfExtent().y,
                         mtx.halfExtent().z);
        for (const auto& w : vm->wheels)
            std::println("  wheel {} at ({:.3f}, {:.3f}, {:.3f}) radius {:.3f} width {:.3f}", w.index, w.position.x,
                         w.position.y, w.position.z, w.radius, w.width);
    }
    return 0;
}

// Writes `pkg` (one LOD, one paint job) as OBJ + MTL, with PNG textures.
int cmdPkg2Obj(std::span<char* const> args) {
    if (args.size() < 3) {
        std::println(stderr, "usage: pkg2obj <container> <pkg> <out.obj> [lod H|M|L|VL] [paintjob N]");
        return 2;
    }
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    asset::Lod lod = asset::Lod::High;
    std::size_t paintjob = 0;
    for (std::size_t i = 3; i + 1 < args.size(); i += 2) {
        const std::string_view key = args[i];
        if (key == "lod")
            lod = asset::splitLodName(args[i + 1]).second;
        else if (key == "paintjob")
            paintjob = static_cast<std::size_t>(str::parseInt(args[i + 1]).value_or(0));
    }
    const std::string base = baseName(args[1]);
    std::string err;
    auto vm = asset::loadVehicleModel(base,
                                      [&](std::string_view p) -> std::optional<std::vector<std::byte>> {
                                          auto f = fs->open(p);
                                          if (!f)
                                              return std::nullopt;
                                          return f->readAll();
                                      },
                                      &err);
    if (!vm) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    const auto& pkg = vm->pkg;
    if (pkg.paintjobs.empty()) {
        std::println(stderr, "error: model has no materials");
        return 1;
    }
    paintjob = std::min(paintjob, pkg.paintjobs.size() - 1);
    const auto& materials = pkg.paintjobs[paintjob];

    const auto objPath = str::toPath(args[2]);
    const auto outDir = objPath.parent_path();
    const std::string mtlName = str::fromPath(objPath.stem()) + ".mtl";
    std::string obj = std::format("# {} exported by mm2tool (lod {}, paint job {})\nmtllib {}\n", args[1],
                                  lodName(lod), paintjob, mtlName);
    std::string mtl;
    std::set<std::size_t> usedMaterials;
    std::size_t vertexBase = 1;
    for (const auto& part : pkg.parts()) {
        const auto* mesh = pkg.findBest(part, lod);
        if (!mesh)
            continue;
        // Parts with a pivot (wheels, lights, breakables) are stored around
        // their own origin; the pivot places them in model space.
        Vec3 offset{};
        if (const auto* pv = vm->pivot(part))
            offset = pv->origin;
        obj += std::format("o {}\n", mesh->name);
        for (const auto& section : mesh->sections) {
            const std::size_t mi = section.shaderIndex;
            usedMaterials.insert(mi);
            obj += std::format("usemtl m{}\n", mi);
            for (const auto& packet : section.packets) {
                for (const auto& v : packet.vertices) {
                    const Vec3 p = v.position + offset;
                    obj += std::format("v {} {} {}\nvt {} {}\nvn {} {} {}\n", p.x, p.y, p.z, v.uv.x, v.uv.y,
                                       v.normal.x, v.normal.y, v.normal.z);
                }
                for (std::size_t i = 0; i + 2 < packet.indices.size(); i += 3) {
                    const std::size_t a = vertexBase + packet.indices[i], b = vertexBase + packet.indices[i + 1],
                                      c = vertexBase + packet.indices[i + 2];
                    obj += std::format("f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n", a, b, c);
                }
                vertexBase += packet.vertices.size();
            }
        }
    }
    for (std::size_t mi : usedMaterials) {
        mtl += std::format("newmtl m{}\n", mi);
        if (mi >= materials.size())
            continue;
        const auto& m = materials[mi];
        mtl += std::format("Kd {} {} {}\nd {}\n", m.diffuse.x, m.diffuse.y, m.diffuse.z, m.diffuse.w);
        if (m.texture.empty())
            continue;
        if (auto texPath = findTexture(*fs, m.texture)) {
            auto data = readFile(*fs, *texPath);
            std::string terr;
            auto img = data ? asset::decodeImageFile(*texPath, *data, &terr) : std::nullopt;
            if (img) {
                const std::string png = m.texture + ".png";
                file::writeAtomic(outDir / png, asset::encodePng(img->levels[0]));
                mtl += std::format("map_Kd {}\n", png);
                if (img->hasTranslucency())
                    mtl += std::format("map_d -imfchan m {}\n", png);
            }
        } else {
            std::println(stderr, "warning: texture '{}' not found", m.texture);
        }
    }
    if (!file::writeAtomic(objPath, obj) || !file::writeAtomic(outDir / mtlName, mtl)) {
        std::println(stderr, "error: cannot write output");
        return 1;
    }
    std::println("wrote {} ({} vertices)", args[2], vertexBase - 1);
    return 0;
}

std::optional<asset::BoundGeometry> loadBound(const vfs::FileSystem& fs, const std::string& path, std::string& err) {
    auto data = readFile(fs, path);
    if (!data)
        return std::nullopt;
    if (str::iendsWith(path, ".bbnd"))
        return asset::parseBbnd(*data, &err);
    return asset::parseBnd(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()), &err);
}

int cmdBndInfo(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    const std::string path = args[1];
    std::string err;
    if (str::iendsWith(path, ".ter")) {
        auto data = readFile(*fs, path);
        auto t = data ? asset::parseTer(*data, &err) : std::nullopt;
        if (!t) {
            std::println(stderr, "error: {}", err);
            return 1;
        }
        std::println("terrain bound v{:.2f}: {} polygons, {} edges, grid {}x{}x{} ({} references), hot edges {}",
                     t->version, t->polygonCount(), t->edges.size(), t->widthSections, t->heightSections,
                     t->depthSections, t->sectionPolygons.size(), t->useHotEdges ? "on" : "off");
        std::println("  box ({:.3f},{:.3f},{:.3f})..({:.3f},{:.3f},{:.3f}) size ({:.3f},{:.3f},{:.3f})", t->min.x,
                     t->min.y, t->min.z, t->max.x, t->max.y, t->max.z, t->size.x, t->size.y, t->size.z);
        return 0;
    }
    auto g = loadBound(*fs, path, err);
    if (!g) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    const auto b = g->bounds();
    std::size_t quads = 0;
    for (const auto& p : g->polygons)
        quads += p.isQuad();
    std::println("{} vertices, {} polygons ({} quads), box ({:.3f},{:.3f},{:.3f})..({:.3f},{:.3f},{:.3f})",
                 g->vertices.size(), g->polygons.size(), quads, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
    for (const auto& m : g->materials)
        std::println("  material {:12} elasticity {:.3f} friction {:.3f} effect {} sound {}", m.name, m.elasticity,
                     m.friction, m.effect, m.sound);
    return 0;
}

int cmdBnd2Obj(std::span<char* const> args) {
    if (args.size() < 3)
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    std::string err;
    auto g = loadBound(*fs, args[1], err);
    if (!g) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    std::string obj = std::format("# {} exported by mm2tool\n", args[1]);
    for (const auto& v : g->vertices)
        obj += std::format("v {} {} {}\n", v.x, v.y, v.z);
    for (const auto& p : g->polygons) {
        obj += "f";
        for (int i = 0; i < p.vertexCount(); ++i)
            obj += std::format(" {}", p.indices[static_cast<std::size_t>(i)] + 1);
        obj += '\n';
    }
    if (!file::writeAtomic(str::toPath(args[2]), obj)) {
        std::println(stderr, "error: cannot write output");
        return 1;
    }
    return 0;
}

// Parses every asset this module understands and reports per-type results.
int cmdAssetCheck(std::span<char* const> args) {
    if (args.empty())
        return 2;
    auto fs = openContainer(args[0]);
    if (!fs)
        return 1;
    const bool verbose = args.size() > 1 && std::string_view(args[1]) == "-v";
    struct Stat {
        int ok = 0, failed = 0, warned = 0, known = 0;
    };
    std::map<std::string, Stat> stats;
    std::map<std::string, int> texFormats;
    std::set<std::string> missingTextures;
    std::set<std::string> textureNames;

    std::vector<vfs::EntryInfo> files;
    fs->forEachFile([&](const vfs::EntryInfo& e) { files.push_back(e); });
    std::ranges::sort(files, {}, &vfs::EntryInfo::path);

    for (const auto& e : files) {
        const auto dot = e.path.rfind('.');
        if (dot == std::string::npos)
            continue;
        const std::string ext = e.path.substr(dot + 1);
        static const std::set<std::string> known = {"tex", "tga", "jpg", "bmp", "pkg", "mtx", "bnd", "bbnd", "ter"};
        if (!known.contains(ext))
            continue;
        // aud/dmusic/*.bnd are DirectMusic band files, not collision bounds.
        if (ext == "bnd" && !e.path.starts_with("bound/"))
            continue;
        auto data = fs->open(e.path) ? std::optional(fs->open(e.path)->readAll()) : std::nullopt;
        std::string err;
        bool ok = false;
        std::vector<std::string> warnings;
        if (!data) {
            err = "unreadable";
        } else if (ext == "tex") {
            auto t = asset::parseTex(*data, &err);
            ok = t.has_value();
            if (t)
                ++texFormats[std::string(formatName(t->header.format))];
        } else if (ext == "tga" || ext == "jpg" || ext == "bmp") {
            ok = asset::decodeImageFile(e.path, *data, &err).has_value();
        } else if (ext == "pkg") {
            auto p = asset::parsePkg(*data, &err);
            ok = p.has_value();
            if (p) {
                warnings = p->warnings;
                for (const auto& pj : p->paintjobs)
                    for (const auto& m : pj)
                        if (!m.texture.empty())
                            textureNames.insert(str::lower(m.texture));
            }
        } else if (ext == "mtx") {
            ok = asset::parseMtx(*data, &err).has_value();
        } else if (ext == "bnd") {
            ok = asset::parseBnd(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()), &err)
                     .has_value();
        } else if (ext == "bbnd") {
            ok = asset::parseBbnd(*data, &err).has_value();
        } else if (ext == "ter") {
            ok = asset::parseTer(*data, &err).has_value();
        }
        auto& s = stats[ext];
        if (!ok && asset::isKnownBrokenRetailAsset(e.path)) {
            ++s.known;
            if (verbose)
                std::println("KNOWN {}: broken in the retail data ({})", e.path, err);
            continue;
        }
        if (ok) {
            ++s.ok;
            if (!warnings.empty()) {
                ++s.warned;
                if (verbose)
                    for (const auto& w : warnings)
                        std::println("WARN {}: {}", e.path, w);
            }
        } else {
            ++s.failed;
            std::println("FAIL {}: {}", e.path, err);
        }
    }
    for (const auto& name : textureNames)
        if (!findTexture(*fs, name))
            missingTextures.insert(name);

    int failures = 0;
    for (const auto& [ext, s] : stats) {
        std::println("{:5} {:5} ok {:3} failed {:3} with warnings {:3} known-broken retail files", ext, s.ok,
                     s.failed, s.warned, s.known);
        failures += s.failed;
    }
    std::print("tex formats:");
    for (const auto& [f, n] : texFormats)
        std::print(" {}={}", f, n);
    std::println("\nmaterial textures referenced: {}, not found: {}", textureNames.size(), missingTextures.size());
    if (verbose)
        for (const auto& m : missingTextures)
            std::println("  missing texture {}", m);
    return failures ? 1 : 0;
}

const Registrar r1({"texinfo", "<container> [glob]", "list .tex headers (size, format, mips, flags)", &cmdTexInfo});
const Registrar r2({"tex2png", "<container> <glob> <outdir> [--mips]", "convert tex/tga/jpg/bmp images to PNG",
                    &cmdTex2Png});
const Registrar r3({"pkginfo", "<container> <pkg>", "describe a PKG model (meshes, materials, pivots)",
                    &cmdPkgInfo});
const Registrar r4({"pkg2obj", "<container> <pkg> <out.obj> [lod H|M|L|VL] [paintjob N]",
                    "export a PKG model as Wavefront OBJ with PNG textures", &cmdPkg2Obj});
const Registrar r5({"bndinfo", "<container> <bnd|bbnd|ter>", "describe a collision bound", &cmdBndInfo});
const Registrar r6({"bnd2obj", "<container> <bnd|bbnd> <out.obj>", "export a collision bound as OBJ", &cmdBnd2Obj});
const Registrar r7({"assetcheck", "<container> [-v]", "parse every tex/tga/jpg/pkg/mtx/bnd/bbnd/ter and report",
                    &cmdAssetCheck});

} // namespace
} // namespace mm2::tool
