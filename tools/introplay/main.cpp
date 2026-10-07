// introplay: checks and plays the intro movie (LOGOS.AVI).
//
//   introplay <source> --compare-yuv ffmpeg.yuv   compare decoded YUV 4:1:0 with a raw dump
//   introplay <source> --compare-rgba ffmpeg.rgba compare RGBA output with a raw dump
//   introplay <source> --png <dir> <frame>...     save frames as PNG
//   introplay <source> [--frames N --screenshot out.png] [--backend vulkan|opengl]
//                                                 play it in a window with the game's intro screen
//
// <source> is LOGOS.AVI itself or a game source (disc image, disc, install folder).
#include "app/Context.h"
#include "app/IntroScreen.h"
#include "app/Screens.h"
#include "core/File.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Clock.h"
#include "platform/Platform.h"
#include "render/ImageUtil.h"
#include "video/Movie.h"

#include <SDL3/SDL_main.h>
#include <imgui.h>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <print>

using namespace mm2;

namespace {

std::shared_ptr<const RandomAccessFile> openMovieFile(const std::string& arg, std::optional<vfs::GameSource>* sourceOut) {
    if (str::iendsWith(arg, ".avi"))
        return OsFile::open(str::toPath(arg));
    std::string err;
    auto source = vfs::probeGameSource(str::toPath(arg), &err);
    if (!source) {
        std::println(stderr, "error: {}", err);
        return nullptr;
    }
    if (sourceOut)
        *sourceOut = source;
    auto f = vfs::openSourceFile(*source, "LOGOS.AVI", &err);
    if (!f)
        std::println(stderr, "error: {}", err);
    return f;
}

int compareYuv(video::Movie& movie, const std::string& path) {
    auto ref = file::readBinary(str::toPath(path));
    if (!ref) {
        std::println(stderr, "error: cannot read {}", path);
        return 1;
    }
    const std::size_t w = static_cast<std::size_t>(movie.width()), h = static_cast<std::size_t>(movie.height());
    const std::size_t cw = (w + 3) / 4, ch = (h + 3) / 4;
    const std::size_t frameBytes = w * h + 2 * cw * ch;
    const std::size_t refFrames = ref->size() / frameBytes;
    std::println("decoder frames {}, reference frames {}", movie.frameCount(), refFrames);
    std::uint64_t diff[3] = {}, count[3] = {}, mismatched = 0;
    int maxDiff = 0;
    video::YuvFrame f;
    for (std::size_t i = 0; i < refFrames && movie.decodeNext(f); ++i) {
        const auto* r = reinterpret_cast<const std::uint8_t*>(ref->data()) + i * frameBytes;
        const std::vector<std::uint8_t>* planes[3] = {&f.y, &f.u, &f.v};
        const std::size_t sizes[3] = {w * h, cw * ch, cw * ch};
        std::size_t off = 0;
        bool frameDiffers = false;
        for (int p = 0; p < 3; ++p) {
            for (std::size_t k = 0; k < sizes[p]; ++k) {
                const int d = std::abs(static_cast<int>((*planes[p])[k]) - static_cast<int>(r[off + k]));
                diff[p] += static_cast<std::uint64_t>(d);
                maxDiff = std::max(maxDiff, d);
                frameDiffers |= d != 0;
            }
            count[p] += sizes[p];
            off += sizes[p];
        }
        mismatched += frameDiffers;
    }
    std::println("mean abs diff Y {:.6f} U {:.6f} V {:.6f}, max {}, frames differing {}",
                 static_cast<double>(diff[0]) / static_cast<double>(std::max<std::uint64_t>(count[0], 1)),
                 static_cast<double>(diff[1]) / static_cast<double>(std::max<std::uint64_t>(count[1], 1)),
                 static_cast<double>(diff[2]) / static_cast<double>(std::max<std::uint64_t>(count[2], 1)), maxDiff,
                 mismatched);
    return mismatched ? 1 : 0;
}

int compareRgba(video::Movie& movie, const std::string& path) {
    auto ref = file::readBinary(str::toPath(path));
    if (!ref) {
        std::println(stderr, "error: cannot read {}", path);
        return 1;
    }
    const std::size_t frameBytes = static_cast<std::size_t>(movie.width() * movie.height() * 4);
    const std::size_t refFrames = ref->size() / frameBytes;
    std::uint64_t diff[3] = {};
    std::uint64_t n = 0;
    int maxDiff = 0;
    std::vector<std::uint8_t> rgba;
    for (std::size_t i = 0; i < refFrames && movie.decodeNextRgba(rgba); ++i) {
        const auto* r = reinterpret_cast<const std::uint8_t*>(ref->data()) + i * frameBytes;
        for (std::size_t k = 0; k < frameBytes; k += 4) {
            for (int c = 0; c < 3; ++c) {
                const int d = std::abs(static_cast<int>(rgba[k + static_cast<std::size_t>(c)]) -
                                       static_cast<int>(r[k + static_cast<std::size_t>(c)]));
                diff[c] += static_cast<std::uint64_t>(d);
                maxDiff = std::max(maxDiff, d);
            }
            ++n;
        }
    }
    std::println("RGBA vs reference over {} frames: mean abs diff R {:.3f} G {:.3f} B {:.3f}, max {}", refFrames,
                 static_cast<double>(diff[0]) / static_cast<double>(n), static_cast<double>(diff[1]) / static_cast<double>(n),
                 static_cast<double>(diff[2]) / static_cast<double>(n), maxDiff);
    return 0;
}

int savePngs(video::Movie& movie, const std::string& dir, const std::vector<int>& wanted) {
    std::vector<std::uint8_t> rgba;
    int maxWanted = 0;
    for (int w : wanted)
        maxWanted = std::max(maxWanted, w);
    for (int i = 0; i <= maxWanted && movie.decodeNextRgba(rgba); ++i) {
        if (std::ranges::find(wanted, i) == wanted.end())
            continue;
        render::Image img{static_cast<std::uint32_t>(movie.width()), static_cast<std::uint32_t>(movie.height()), rgba};
        const auto path = str::toPath(dir) / std::format("logos_{:03}.png", i);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (!render::writePng(path, img))
            return 1;
        std::println("wrote {}", str::fromPath(path));
    }
    return 0;
}

// Runs the game's intro screen in a window (a cut-down App::run).
int play(const vfs::GameSource& source, int frames, const std::string& screenshot, const std::string& backend) {
    std::string error;
    if (!platform::init({}, &error)) {
        std::println(stderr, "platform: {}", error);
        return 1;
    }
    app::Context ctx;
    if (!backend.empty())
        render::parseBackend(backend, ctx.display.backend);
    ctx.display.windowWidth = 960;
    ctx.display.windowHeight = 720;
    render::RendererConfig rc;
    rc.settings = ctx.display;
    rc.title = "introplay";
    auto renderer = render::createRenderer(rc, &error);
    if (!renderer) {
        std::println(stderr, "renderer: {}", error);
        return 1;
    }
    ctx.renderer = std::move(*renderer);
    ImGui::CreateContext(); // the overlay does not need it, but Context users may
    ctx.overlay = std::make_unique<render::Overlay2D>(ctx.device());
    ctx.mixer = std::make_shared<audio::Mixer>(48000);
    if (!ctx.audioDevice.open(ctx.mixer, &error))
        std::println(stderr, "audio: {} (playing silently on the wall clock)", error);
    ctx.game = app::loadGameData(source, &error);
    if (!ctx.game) {
        std::println(stderr, "game data: {}", error);
        return 1;
    }

    auto screen = app::makeIntroScreen(ctx);
    platform::FrameClock clock;
    int frame = 0;
    bool done = false;
    while (!done) {
        ctx.input.beginFrame();
        const auto ev = platform::pollEvents(&ctx.input);
        if (ev.quitRequested)
            break;
        if (ev.resized)
            ctx.device().notifyResized();
        const double dt = clock.tick();
        auto& dev = ctx.device();
        if (!dev.beginFrame()) {
            platform::sleepPrecise(0.01);
            continue;
        }
        screen->update(ctx, dt);
        dev.beginOverlay({0, 0, 0, 1});
        screen->drawOverlay(ctx);
        dev.endOverlay();
        const bool last = frames > 0 && frame + 1 >= frames;
        if (last && !screenshot.empty())
            dev.requestCapture();
        dev.endFrame();
        ++frame;
        if (last) {
            if (!screenshot.empty()) {
                render::Image img;
                if (dev.readCapture(img) && render::writePng(str::toPath(screenshot), img))
                    std::println("screenshot {}", screenshot);
            }
            done = true;
        }
        // The intro hands over to the frontend when it ends: stop there.
        if (ctx.nextScreen) {
            std::println("intro finished after {} frames ({:.2f} s)", frame, clock.elapsed());
            done = true;
        }
    }
    ctx.device().waitIdle();
    ctx.nextScreen.reset();
    screen.reset();
    ctx.audioDevice.close();
    ctx.overlay.reset();
    ImGui::DestroyContext();
    ctx.renderer.device.reset();
    ctx.renderer.window.reset();
    platform::shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    log::setLevel(std::getenv("INTROPLAY_DEBUG") ? log::Level::Debug : log::Level::Warn);
    if (argc < 2) {
        std::println(stderr, "usage: introplay <LOGOS.AVI|game-source> [--compare-yuv f | --compare-rgba f | "
                             "--png dir N... | --frames N --screenshot out.png --backend name]");
        return 2;
    }
    std::optional<vfs::GameSource> source;
    auto file = openMovieFile(argv[1], &source);
    if (!file)
        return 1;
    std::string err;
    auto movie = video::Movie::open(file, &err);
    if (!movie) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    const std::string mode = argc > 2 ? argv[2] : "";
    if (mode == "--compare-yuv" && argc > 3)
        return compareYuv(*movie, argv[3]);
    if (mode == "--compare-rgba" && argc > 3)
        return compareRgba(*movie, argv[3]);
    if (mode == "--png" && argc > 3) {
        std::vector<int> frames;
        for (int i = 4; i < argc; ++i)
            frames.push_back(static_cast<int>(str::parseInt(argv[i]).value_or(0)));
        return savePngs(*movie, argv[3], frames);
    }
    int frames = 0;
    std::string screenshot, backend;
    for (int i = 2; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames")
            frames = static_cast<int>(str::parseInt(argv[++i]).value_or(0));
        else if (a == "--screenshot")
            screenshot = argv[++i];
        else if (a == "--backend")
            backend = argv[++i];
    }
    if (!source) {
        std::println(stderr, "error: window playback needs a game source, not a bare .avi");
        return 2;
    }
    return play(*source, frames, screenshot, backend);
}
