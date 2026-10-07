// Renders the race HUD with the real renderer and saves a screenshot.
// Opens a window briefly, so it only runs when OPENMM2_HUD_SCREENSHOT names
// the output PNG (and OPENMM2_GAME_DATA is set):
//
//   OPENMM2_HUD_SCREENSHOT=local/out/hud/hud.png test_game --gtest_filter=SessionHud.*
#include "TestData.h"
#include "city/CityData.h"
#include "core/StringUtil.h"
#include "game/ModelLibrary.h"
#include "game/Strings.h"
#include "game/TextureLibrary.h"
#include "game/session/Hud.h"
#include "game/session/Session.h"
#include "platform/Platform.h"
#include "render/ImageUtil.h"
#include "render/Overlay2D.h"
#include "render/Renderer.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <format>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

Mat34 facing(const Vec3& forward, const Vec3& pos) {
    Mat34 m;
    m.m2 = -forward;
    m.m1 = Vec3::yAxis();
    m.m0 = m.m1.cross(m.m2).normalized();
    m.m3 = pos;
    return m;
}

void renderHud(const std::string& out, GameMode mode, int race, int opponents, bool dash, int width, int height) {
    const auto source = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
    ASSERT_TRUE(source);
    const Strings strings = Strings::load(*source);
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);

    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "london";
    cfg.raceIndex = race;
    cfg.opponents = opponents;
    std::string error;
    auto session = Session::create(cfg, *city, *test::gameData(), strings, &error);
    ASSERT_TRUE(session) << error;

    // Drive halfway to the second checkpoint after the countdown.
    PlayerState player;
    player.transform = session->playerSpawn();
    player.speedMph = 63.0f;
    player.rpm = 5400.0f;
    player.maxRpm = 8500.0f;
    player.gear = 3;
    player.damage01 = 0.35f;
    session->start();
    for (int i = 0; i < 200 && session->phase() == Phase::Countdown; ++i)
        session->update(1.0f / 30.0f, player);
    const auto& cps = session->checkpoints();
    const Vec3 a = cps[0].position, b = cps[1].position;
    for (int i = 1; i <= 60; ++i) {
        const Vec3 p = lerp(a, b, static_cast<float>(i) / 120.0f);
        player.transform = facing((b - a).normalized(), p);
        session->update(1.0f / 30.0f, player);
    }
    session->takeEvents();

    std::string err;
    ASSERT_TRUE(platform::init({}, &err)) << err;
    render::RendererConfig rc;
    rc.settings.windowWidth = width;
    rc.settings.windowHeight = height;
    rc.settings.vsync = render::VsyncMode::Off;
    rc.title = "OpenMM2 HUD test";
    auto renderer = render::createRenderer(rc, &err);
    ASSERT_TRUE(renderer) << err;
    {
        render::Device& dev = *renderer->device;
        TextureLibrary textures(dev, *test::gameData());
        ModelLibrary models(dev, *test::gameData());
        ui::TextureCache art(dev, *test::gameData());
        ui::TextRenderer text(dev);
        render::Overlay2D overlay(dev);
        Hud hud(dev, textures, models, *test::gameData(), strings, "london", "vpbug");
        hud.options().dashboard = dash;
        hud.preload(&art);

        std::vector<MapBlip> blips;
        for (std::size_t i = 0; i < session->opponents().size(); ++i)
            blips.push_back({session->opponents()[i].spawn, MapBlip::Kind::Opponent});

        Camera cam;
        const Vec3 fwd = -player.transform.m2;
        if (dash) {
            cam.transform = player.transform;
            cam.transform.m3 = player.transform.m3 + Vec3{0, 1.19f, 0} + fwd * 0.55f;
        } else {
            cam.transform = Camera::lookAt(player.transform.m3 - fwd * 6.5f + Vec3{0, 2.3f, 0},
                                           player.transform.m3 + fwd * 4.0f + Vec3{0, 1.0f, 0});
        }
        // Warm-up frame: creating buffers inside the very first frame of a new
        // Vulkan device crashes in the driver (render module issue).
        {
            ASSERT_TRUE(dev.beginFrame());
            dev.beginOverlay({0, 0, 0, 1});
            dev.endOverlay();
            dev.endFrame();
        }
        const int frames = 6;
        for (int f = 0; f < frames; ++f) {
            ASSERT_TRUE(dev.beginFrame());
            render::ClearValues clear;
            clear.color = {0.35f, 0.42f, 0.5f, 1.0f};
            dev.beginScene(clear);
            const auto ext = dev.sceneExtent();
            render::FrameConstants fc;
            fc.view = cam.view();
            fc.proj = Mat44::perspective(0.9f, static_cast<float>(ext.width) / static_cast<float>(ext.height), 0.1f,
                                         1000.0f, true);
            fc.cameraPosition = cam.position();
            fc.lights[0] = {Vec3{0.3f, -1.0f, -0.4f}.normalized(), {1, 1, 1}};
            fc.ambient = {0.5f, 0.5f, 0.5f};
            dev.setFrameConstants(fc);
            hud.drawWorld(*session, cam, player, 0.3f);
            hud.drawMap(*session, player, blips, 1.0f / 30.0f);
            dev.endScene();
            dev.beginOverlay({0, 0, 0, 1});
            hud.drawOverlay(overlay, text, art, *session, player);
            dev.endOverlay();
            if (f == frames - 1)
                dev.requestCapture();
            dev.endFrame();
        }
        render::Image image;
        ASSERT_TRUE(dev.readCapture(image));
        EXPECT_TRUE(render::writePng(str::toPath(out), image));
        dev.waitIdle();
    }
    renderer->device.reset();
    renderer->window.reset();
    platform::shutdown();
}

} // namespace

TEST(SessionHud, Screenshot) {
    MM2_REQUIRE_GAME_DATA();
    const char* out = std::getenv("OPENMM2_HUD_SCREENSHOT");
    if (!out || !*out)
        GTEST_SKIP() << "set OPENMM2_HUD_SCREENSHOT to render the HUD";
    const std::string base(out);
    const std::string stem = base.ends_with(".png") ? base.substr(0, base.size() - 4) : base;
    renderHud(stem + "_blitz.png", GameMode::Blitz, 0, 0, false, 1280, 720);
    renderHud(stem + "_circuit.png", GameMode::Circuit, 0, 5, false, 1280, 960);
    renderHud(stem + "_dash.png", GameMode::Checkpoint, 0, 3, true, 1280, 720);
}
