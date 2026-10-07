// Effects and bangers: bangers, fxsim.
#include "Command.h"
#include "Common.h"

#include "asset/Image.h"
#include "city/CityData.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/PropPlacement.h"
#include "game/fx/EffectLibrary.h"
#include "game/fx/Particles.h"
#include "vfs/GameSource.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <print>

namespace mm2::tool {
namespace {

bool mountSource(const char* arg, vfs::Vfs& v) {
    std::string err;
    auto src = vfs::probeGameSource(str::toPath(arg), &err);
    if (!src || !vfs::mountGameSource(v, *src, &err)) {
        std::println(stderr, "error: {}", err);
        return false;
    }
    return true;
}

struct Canvas {
    int w, h;
    std::vector<float> rgb;
    Canvas(int w_, int h_, Vec3 bg) : w(w_), h(h_), rgb(static_cast<std::size_t>(w_ * h_ * 3)) {
        for (int i = 0; i < w * h; ++i) {
            rgb[static_cast<std::size_t>(i * 3)] = bg.x;
            rgb[static_cast<std::size_t>(i * 3 + 1)] = bg.y;
            rgb[static_cast<std::size_t>(i * 3 + 2)] = bg.z;
        }
    }
    void blend(int x, int y, Vec3 c, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0)
            return;
        float* p = &rgb[static_cast<std::size_t>((y * w + x) * 3)];
        p[0] += (c.x - p[0]) * a;
        p[1] += (c.y - p[1]) * a;
        p[2] += (c.z - p[2]) * a;
    }
    bool save(const std::string& path) const {
        asset::Image::Level lvl;
        lvl.width = static_cast<std::uint32_t>(w);
        lvl.height = static_cast<std::uint32_t>(h);
        lvl.rgba.resize(static_cast<std::size_t>(w * h * 4));
        // encodePng flips rows (game images are bottom-up), so write bottom-up.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float* s = &rgb[static_cast<std::size_t>((y * w + x) * 3)];
                std::uint8_t* d = &lvl.rgba[static_cast<std::size_t>(((h - 1 - y) * w + x) * 4)];
                for (int k = 0; k < 3; ++k)
                    d[k] = static_cast<std::uint8_t>(std::clamp(s[k], 0.0f, 1.0f) * 255.0f + 0.5f);
                d[3] = 255;
            }
        const auto png = asset::encodePng(lvl);
        return file::writeAtomic(str::toPath(path), std::span<const std::byte>(png));
    }
};

// bangers <source> <city> [--png out.png]
int cmdBangers(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    vfs::Vfs v;
    if (!mountSource(args[0], v))
        return 1;
    std::string err;
    auto city = city::loadCity(v, args[1], &err);
    if (!city) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    game::bangers::BangerDataLibrary data(v);
    const auto props = game::bangers::placeCityProps(*city, v, data);
    std::map<std::string, int> byModel;
    std::map<int, int> bySource;
    int withData = 0;
    for (const auto& p : props) {
        ++byModel[p.model];
        ++bySource[static_cast<int>(p.source)];
        if (data.find(p.model))
            ++withData;
    }
    std::println("{}: {} banger data files, {} props placed ({} with banger data)", args[1], data.available(),
                 props.size(), withData);
    static const char* kSource[] = {"instances", "props.pathset", "street rules", "race"};
    for (auto [s, n] : bySource)
        std::println("  {:14} {}", kSource[s], n);
    for (const auto& [m, n] : byModel) {
        const auto* d = data.find(m);
        std::println("  {:28} x{:<5} mass {:8.1f}  limit {:9.1f}  parts {}  tex {}", m, n, d ? d->mass : 0.0f,
                     d ? std::sqrt(d->impulseLimit2) : 0.0f, d ? d->numParts : 0, d ? d->texNumber : 0);
    }
    if (args.size() >= 4 && std::string_view(args[2]) == "--png") {
        const Aabb b = city->psdl.bounds;
        const int W = 2048;
        const float scale = static_cast<float>(W) / std::max(b.max.x - b.min.x, b.max.z - b.min.z);
        const int H = static_cast<int>((b.max.z - b.min.z) * scale) + 1;
        Canvas c(W, H, {0.05f, 0.05f, 0.08f});
        auto toPx = [&](const Vec3& p) {
            return std::pair{static_cast<int>((p.x - b.min.x) * scale), static_cast<int>((p.z - b.min.z) * scale)};
        };
        for (const auto& vtx : city->psdl.vertices) {
            auto [x, y] = toPx(vtx);
            c.blend(x, y, {0.35f, 0.35f, 0.4f}, 1.0f);
        }
        for (const auto& p : props) {
            const Vec3 col = p.source == game::bangers::PlacedProp::Source::StreetRule ? Vec3{1.0f, 0.85f, 0.2f}
                             : p.source == game::bangers::PlacedProp::Source::PathSet ? Vec3{0.3f, 1.0f, 0.3f}
                                                                                       : Vec3{1.0f, 0.2f, 0.2f};
            auto [x, y] = toPx(p.transform.m3);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    c.blend(x + dx, y + dy, col, 1.0f);
            // +X axis tick (lamp arms point the other way, over the road).
            for (int k = 2; k < 6; ++k) {
                auto [tx, ty] = toPx(p.transform.m3 + p.transform.m0 * (static_cast<float>(k) / scale));
                c.blend(tx, ty, {0.4f, 0.8f, 1.0f}, 0.8f);
            }
        }
        if (!c.save(args[3]))
            return 1;
        std::println("wrote {}", args[3]);
    }
    return 0;
}

// fxsim <source> <rule|banger:model> <outdir> [--frames N] [--every K]
int cmdFxSim(std::span<char* const> args) {
    if (args.size() < 3)
        return 2;
    vfs::Vfs v;
    if (!mountSource(args[0], v))
        return 1;
    game::fx::EffectLibrary lib;
    lib.load(v);
    std::string name = args[1];
    game::fx::BirthRule rule;
    game::fx::ParticleSheet sheet = game::fx::EffectLibrary::wheelSheet();
    if (name.starts_with("banger:")) {
        game::bangers::BangerDataLibrary data(v);
        const auto* d = data.find(name.substr(7));
        if (!d || !d->birthRule) {
            std::println(stderr, "error: no banger birth rule for {}", name);
            return 1;
        }
        rule = *d->birthRule;
        sheet = game::fx::EffectLibrary::bangerSheet(std::max(1, d->texNumber));
    } else if (const auto* r = lib.rule(name)) {
        rule = *r;
        if (name == "rain" || name == "snow")
            sheet = game::fx::EffectLibrary::rainSheet();
    } else {
        std::println(stderr, "error: unknown rule '{}'", name);
        return 1;
    }
    rule.position = {};
    int frames = 60, every = 10;
    for (std::size_t i = 3; i + 1 < args.size(); i += 2) {
        if (std::string_view(args[i]) == "--frames")
            frames = static_cast<int>(str::parseInt(args[i + 1]).value_or(60));
        else if (std::string_view(args[i]) == "--every")
            every = static_cast<int>(str::parseInt(args[i + 1]).value_or(10));
    }
    // Particle sheet texture.
    std::optional<asset::Image> tex;
    if (auto bytes = v.readAll("texture/" + sheet.texture + ".tex"))
        if (auto t = asset::parseTex(*bytes))
            tex = std::move(t->image);
    if (!tex)
        if (auto bytes = v.readAll("texture/" + sheet.texture + ".tga"))
            tex = asset::decodeTga(*bytes);

    game::fx::ParticleSystem sys;
    sys.init(std::max(64, rule.initialBlast + static_cast<int>(rule.spewRate * 4)), sheet.framesWide, sheet.framesHigh);
    sys.setBirthRule(&rule);
    sys.blast(rule.initialBlast > 0 ? rule.initialBlast : 0);
    const float dt = 1.0f / 30.0f;
    const auto outDir = str::toPath(args[2]);
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    for (int f = 0; f <= frames; ++f) {
        if (f % every == 0) {
            // Side view: x right, y up, 32 px per metre, origin at the bottom centre.
            const int W = 512, H = 512;
            const float ppm = 32.0f;
            Canvas c(W, H, {0.15f, 0.2f, 0.3f});
            for (int x = 0; x < W; ++x)
                c.blend(x, H - 32, {0.5f, 0.5f, 0.5f}, 1.0f); // y = 0 line
            for (const auto& p : sys.positions()) {
                const float cx = W * 0.5f + p.position.x * ppm, cy = (H - 32) - p.position.y * ppm;
                const float r = std::max(1.0f, p.radius * ppm);
                const int frame = std::max(0, static_cast<int>(p.frame));
                const int col = frame % sheet.framesWide, row = (frame / sheet.framesWide) % sheet.framesHigh;
                const float alpha = static_cast<float>(p.color >> 24) / 255.0f;
                for (int y = static_cast<int>(cy - r); y <= static_cast<int>(cy + r); ++y)
                    for (int x = static_cast<int>(cx - r); x <= static_cast<int>(cx + r); ++x) {
                        float u = (x - (cx - r)) / (2 * r), vv = 1.0f - (y - (cy - r)) / (2 * r); // card v up
                        Vec3 color{1, 1, 1};
                        float a = alpha;
                        if (tex) {
                            const auto& lvl = tex->levels[0];
                            const float tu = (u + col) / sheet.framesWide, tv = (vv + row) / sheet.framesHigh;
                            const auto tx = std::min<std::uint32_t>(static_cast<std::uint32_t>(tu * lvl.width), lvl.width - 1);
                            const auto ty = std::min<std::uint32_t>(static_cast<std::uint32_t>(tv * lvl.height), lvl.height - 1);
                            const std::uint8_t* t = &lvl.rgba[(ty * lvl.width + tx) * 4];
                            color = {t[0] / 255.0f, t[1] / 255.0f, t[2] / 255.0f};
                            a *= t[3] / 255.0f;
                        }
                        c.blend(x, y, color, a);
                    }
            }
            const std::string path = str::fromPath(outDir / std::format("{}_{:03}.png", str::lower(name.substr(name.find(':') + 1)), f));
            c.save(path);
            std::println("frame {:3}: {:4} particles -> {}", f, sys.count(), path);
        }
        sys.update(dt);
    }
    return 0;
}

const Registrar r1({"bangers", "<game-source> <city> [--png out.png]",
                    "list the breakable props of a city (instances, props.pathset, street rules)", &cmdBangers});
const Registrar r2({"fxsim", "<game-source> <rule|banger:model> <outdir> [--frames N] [--every K]",
                    "simulate a particle birth rule and draw side-view PNG frames", &cmdFxSim});

} // namespace
} // namespace mm2::tool
