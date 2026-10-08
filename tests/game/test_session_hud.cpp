// HUD logic (MM2's mmHUD and friends), HUD data, and a rendered screenshot.
//
// The screenshot test opens a window briefly, so it only runs when
// OPENMM2_HUD_SCREENSHOT names the output PNG (and OPENMM2_GAME_DATA is set):
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
#include "render/Projection.h"
#include "render/Renderer.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <format>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

void expectVec(const Vec3& v, const Vec3& expected, float tol = 1e-4f) {
    EXPECT_NEAR(v.x, expected.x, tol);
    EXPECT_NEAR(v.y, expected.y, tol);
    EXPECT_NEAR(v.z, expected.z, tol);
}

std::string digits(const std::array<char, 3>& d) { return std::string(d.begin(), d.end()); }

} // namespace

// --- Logic (mmHUD, mmExternalView, mmArrow, mmHudMap, ...) --------------------------

TEST(HudLogic, ClockShowsMinutesSecondsHundredths) {
    // mmHUD::Update: +0.005, then MM:SS:HH with the minutes modulo 100.
    EXPECT_EQ(hud::clockText(0.0f), "00:00:00");
    EXPECT_EQ(hud::clockText(-3.0f), "00:00:00");
    EXPECT_EQ(hud::clockText(65.43f), "01:05:43");
    EXPECT_EQ(hud::clockText(59.996f), "01:00:00");
    EXPECT_EQ(hud::clockText(3599.99f), "59:59:99");
    EXPECT_EQ(hud::clockText(6000.0f), "00:00:00");
}

TEST(HudLogic, LapTimesUseGetLocTime) {
    EXPECT_EQ(hud::lapTimeText(0.0f), "  ---  ");
    EXPECT_EQ(hud::lapTimeText(83.456f), "1:23:46");
    EXPECT_EQ(hud::lapTimeText(9.999f), "0:10:00");
}

TEST(HudLogic, SpeedDigitsTruncateAndHideLeadingZeros) {
    EXPECT_EQ(digits(hud::speedDigits(0.0f)), "  0");
    EXPECT_EQ(digits(hud::speedDigits(5.9f)), "  5");
    EXPECT_EQ(digits(hud::speedDigits(63.7f)), " 63");
    EXPECT_EQ(digits(hud::speedDigits(105.0f)), "105");
    EXPECT_EQ(digits(hud::speedDigits(100.2f)), "100");
    // Hundreds past 9 are not drawn, the tens still are.
    EXPECT_EQ(digits(hud::speedDigits(1005.0f)), " 05");
}

TEST(HudLogic, GearArt) {
    EXPECT_EQ(hud::gearArt(-1, false), "r");
    EXPECT_EQ(hud::gearArt(0, true), "p"); // digitac_gear_p shows N
    EXPECT_EQ(hud::gearArt(3, false), "3");
    EXPECT_EQ(hud::gearArt(3, true), "d");
    EXPECT_EQ(hud::gearArt(-1, true), "r");
}

TEST(HudLogic, Gauges) {
    // speed_ticks (129 wide) lit up to rpm / MaxRPM.
    EXPECT_EQ(hud::linearGaugeLength(5400.0f, 8500.0f, 129), 81);
    EXPECT_EQ(hud::linearGaugeLength(0.0f, 8500.0f, 129), 0);
    EXPECT_EQ(hud::linearGaugeLength(9000.0f, 8500.0f, 129), 129);
    // A 129-pixel window sliding across the 500-pixel damage bar.
    EXPECT_EQ(hud::slidingGaugeOffset(0.0f, 1.0f, 500, 129), 0);
    EXPECT_EQ(hud::slidingGaugeOffset(0.35f, 1.0f, 500, 129), 129);
    EXPECT_EQ(hud::slidingGaugeOffset(1.0f, 1.0f, 500, 129), 371);
    // Dash needles: the tachometer rests at 800 of a fixed 8000 rpm.
    EXPECT_NEAR(hud::gaugeAngle(0.0f, 800.0f, 8000.0f, 0.0f, 3.408997f), 0.3408997f, 1e-6f);
    EXPECT_NEAR(hud::gaugeAngle(4000.0f, 800.0f, 8000.0f, 0.0f, 3.408997f), 1.7044985f, 1e-6f);
    EXPECT_NEAR(hud::gaugeAngle(9000.0f, 800.0f, 8000.0f, 0.0f, 3.408997f), 3.408997f, 1e-6f);
    EXPECT_NEAR(hud::gaugeAngle(80.0f, 0.0f, 160.0f, 0.0f, 3.614002f), 1.807001f, 1e-6f);
}

TEST(HudLogic, ArrowPose) {
    Mat34 cam = Mat34::identity();
    cam.m3 = {10, 3, 20};
    // Straight ahead: 2.5 m up, 6.1 m ahead, tilted so the far end dips.
    hud::ArrowPose ahead = hud::arrowPose(cam, {10, 0, -100});
    expectVec(ahead.local.m3, {0.0f, 2.5f, -6.1f});
    const float s = std::sin(20.0f * kDegToRad), c = std::cos(20.0f * kDegToRad);
    expectVec(ahead.local.m2, {0.0f, s, c});
    expectVec(ahead.local.m1, {0.0f, c, -s});
    ASSERT_TRUE(ahead.behind.has_value());
    EXPECT_FALSE(*ahead.behind);
    // Behind: yellow.
    const hud::ArrowPose behind = hud::arrowPose(cam, {10, 0, 100});
    ASSERT_TRUE(behind.behind.has_value());
    EXPECT_TRUE(*behind.behind);
    // Level with the camera: the colour is kept.
    EXPECT_FALSE(hud::arrowPose(cam, {50, 0, 20}).behind.has_value());

    // A pitched camera keeps the target's vertical component, and the
    // side axis is not renormalised.
    const Mat34 pitched = Mat34::rotationX(-0.5f) * cam;
    const hud::ArrowPose p = hud::arrowPose(pitched, {10, 0, -100});
    const Vec3 dir = pitched.untransform({10, 3, -100}).normalized();
    EXPECT_NEAR(p.local.m0.mag(), std::sqrt(dir.x * dir.x + dir.z * dir.z), 1e-5f);
    EXPECT_LT(p.local.m0.mag(), 0.99f);
}

TEST(HudLogic, StandSpansTheGate) {
    Checkpoint cp;
    cp.position = {10, 2, 3};
    cp.headingDeg = 90.0f;
    cp.radius = 12.0f;
    const Mat34 m = hud::standMatrix(cp);
    // (radius, 7.5, radius) scale, centred 3.75 m above the waypoint.
    expectVec(m.m3, {10.0f, 5.75f, 3.0f});
    expectVec(m.m1, {0.0f, 7.5f, 0.0f});
    expectVec(m.m0, Mat34::rotationY(-90.0f * kDegToRad).m0 * 12.0f);
    EXPECT_NEAR(m.m2.mag(), 12.0f, 1e-4f);
    // The model's +-1 x extent reaches the gate ends.
    const Vec3 end = m.transform({1, -0.5f, 0});
    EXPECT_NEAR(Vec2(end.x - cp.position.x, end.z - cp.position.z).mag2(), 144.0f, 1e-2f);
    EXPECT_NEAR(end.y, 2.0f, 1e-4f);
}

TEST(HudLogic, MapCamera) {
    Mat34 car = Mat34::identity();
    car.m3 = {100, 7, -50};
    // Fixed orientation: -Z up, +X right, looking down from the zoom height.
    const Mat34 fixed = hud::mapCamera(car, 1195.0f, false);
    expectVec(fixed.m0, {1, 0, 0});
    expectVec(fixed.m1, {0, 0, -1});
    expectVec(fixed.m2, {0, 1, 0});
    expectVec(fixed.m3, {100, 1195, -50});
    // Rotating: the car's heading points up. A car facing +X:
    car.m0 = {0, 0, -1};
    car.m2 = {-1, 0, 0};
    const Mat34 rot = hud::mapCamera(car, 577.0f, true);
    expectVec(rot.m1, {1, 0, 0});
    expectVec(rot.m0, {0, 0, 1});
    // The rotating camera of a car facing -Z is the fixed one.
    const Mat34 north = hud::mapCamera(Mat34::identity(), 577.0f, true);
    expectVec(north.m1, {0, 0, -1});
}

TEST(HudLogic, MapIconsAndZoom) {
    Mat34 car = Mat34::rotationX(0.3f) * Mat34::rotationY(0.7f);
    car.m3 = {1, 2, 3};
    const Mat34 m = hud::mapIconMatrix(car, 52.5f);
    EXPECT_NEAR(m.m3.y, 17.0f, 1e-4f);
    EXPECT_NEAR(m.m0.mag(), 52.5f, 1e-3f);
    EXPECT_NEAR(m.m0.y, 0.0f, 1e-4f); // the side axis stays horizontal
    // Zooming in moves at (out - in) x Approach Rate per second, then stops.
    const float rate = (1195.0f - 577.0f) * 1.2f;
    float z = hud::approach(1195.0f, 577.0f, rate, 0.5f);
    EXPECT_NEAR(z, 1195.0f - 370.8f, 1e-3f);
    z = hud::approach(z, 577.0f, rate, 0.5f);
    EXPECT_EQ(z, 577.0f);
    EXPECT_EQ(hud::approach(577.0f, 1195.0f, rate, 10.0f), 1195.0f);
    EXPECT_EQ(hud::mapIconColor(hud::MapIcon::Player), 0xFFFFFF00u);
    EXPECT_EQ(hud::mapIconColor(hud::MapIcon::Opponent), 0xFFB400FFu);
    EXPECT_EQ(hud::mapIconColor(hud::MapIcon::Police), 0xFFFF0000u);
    EXPECT_EQ(hud::mapIconColor(hud::MapIcon::Outline), 0xFF000000u);
}

TEST(HudLogic, MapRect) {
    const render::UiLayout l = render::computeUiLayout({640, 480}, render::UiScaleMode::Fit);
    HudMapParams params;
    HudOptions options;
    EXPECT_EQ(options.mapMode, MapMode::Off); // a new player's default (mmStatePack)
    options.mapMode = MapMode::Small;
    // tune/<city>.mmhudmap Pos/Size, less 10 pixels: (499, 360) 124 x 110 at 640x480.
    Vec4 r = hud::mapRect(l, params, options, false);
    EXPECT_NEAR(r.x, 499.2f, 1e-3f);
    EXPECT_NEAR(r.y, 360.0f, 1e-3f);
    EXPECT_NEAR(r.z, 124.4f, 1e-3f);
    EXPECT_NEAR(r.w, 110.0f, 1e-3f);
    // Right-hand-drive cars move it to the left edge while the HUD's
    // dashboard flag is set.
    options.dashActive = true;
    EXPECT_NEAR(hud::mapRect(l, params, options, true).x, 0.0f, 1e-4f);
    EXPECT_NEAR(hud::mapRect(l, params, options, false).x, 499.2f, 1e-3f);
    options.mapMode = MapMode::Split;
    r = hud::mapRect(l, params, options, false);
    EXPECT_NEAR(r.y, 240.0f, 1e-3f);
    EXPECT_NEAR(r.w, 240.0f, 1e-3f);
    options.mapMode = MapMode::Off;
    EXPECT_EQ(hud::mapRect(l, params, options, false).z, 0.0f);
    // Widescreen: fractions of the whole output.
    options.mapMode = MapMode::Small;
    options.dashActive = false;
    const render::UiLayout wide = render::computeUiLayout({1280, 720}, render::UiScaleMode::Fit);
    r = hud::mapRect(wide, params, options, false);
    EXPECT_NEAR(r.x + r.z + 10.0f + 0.01f * (wide.right - wide.left), wide.right, 1e-2f);
}

TEST(HudLogic, ModeRules) {
    LessonEvent lesson;
    // Arrow: none in cruise and circuits, nor in the follow, destroy and map lessons.
    EXPECT_FALSE(hud::arrowShown(GameMode::Circuit, nullptr));
    EXPECT_FALSE(hud::arrowShown(GameMode::Cruise, nullptr));
    EXPECT_TRUE(hud::arrowShown(GameMode::Blitz, nullptr));
    EXPECT_TRUE(hud::arrowShown(GameMode::Checkpoint, nullptr));
    EXPECT_TRUE(hud::arrowShown(GameMode::CopsAndRobbers, nullptr));
    for (LessonType t : {LessonType::Follow, LessonType::Destroy, LessonType::Map}) {
        lesson.type = t;
        EXPECT_FALSE(hud::arrowShown(GameMode::CrashCourse, &lesson));
    }
    lesson.type = LessonType::Course;
    EXPECT_TRUE(hud::arrowShown(GameMode::CrashCourse, &lesson));
    // Clock: not in cruise, C&R and follow lessons; minimum speed only with a limit.
    EXPECT_FALSE(hud::clockShown(GameMode::Cruise, nullptr));
    EXPECT_FALSE(hud::clockShown(GameMode::CopsAndRobbers, nullptr));
    EXPECT_TRUE(hud::clockShown(GameMode::Circuit, nullptr));
    lesson.type = LessonType::Follow;
    EXPECT_FALSE(hud::clockShown(GameMode::CrashCourse, &lesson));
    lesson.type = LessonType::MinimumSpeed;
    lesson.timeLimit = 0.0f;
    EXPECT_FALSE(hud::clockShown(GameMode::CrashCourse, &lesson));
    lesson.timeLimit = 60.0f;
    EXPECT_TRUE(hud::clockShown(GameMode::CrashCourse, &lesson));
    // Check readout hidden for follow and destroy lessons.
    lesson.type = LessonType::Destroy;
    EXPECT_FALSE(hud::checkReadoutShown(GameMode::CrashCourse, &lesson));
    lesson.type = LessonType::Clean;
    EXPECT_TRUE(hud::checkReadoutShown(GameMode::CrashCourse, &lesson));
    EXPECT_FALSE(hud::checkReadoutShown(GameMode::Cruise, nullptr));
}

// --- Data -------------------------------------------------------------------------------

TEST(HudData, MapParamsAndOceanColour) {
    MM2_REQUIRE_GAME_DATA();
    const HudMapParams london = loadHudMapParams(*test::gameData(), "london");
    EXPECT_NEAR(london.pos.x, 0.78f, 1e-6f);
    EXPECT_NEAR(london.size.y, 0.25f, 1e-6f);
    EXPECT_NEAR(london.zoomOutDist, 1195.0f, 1e-3f);
    EXPECT_NEAR(london.iconScaleMax, 52.5f, 1e-3f);
    // mmHudMap::Init replaces the file's Ocean Color.
    expectVec(london.oceanColor, {0.92f, 0.84f, 0.778f});
    const HudMapParams sf = loadHudMapParams(*test::gameData(), "sf");
    expectVec(sf.oceanColor, {0.084f, 0.68f, 0.92f});
    EXPECT_NEAR(sf.zoomOutDistFS, 1581.0f, 1e-3f);
}

TEST(HudData, DashPivotsComeFromTheMtxBoxes) {
    MM2_REQUIRE_GAME_DATA();
    const auto dash = loadDashParams(*test::gameData(), "vpbug");
    ASSERT_TRUE(dash);
    expectVec(dash->dashPos, {0.1314f, -0.6049f, -0.7799f});
    EXPECT_NEAR(dash->rpmRotMax, 3.408997f, 1e-5f);
    // geometry/vpbug_dash_<part>.mtx box centres.
    expectVec(dash->speedPivot, {-0.4556f, 0.2315f, 0.0f}, 1e-3f);
    expectVec(dash->tachPivot, {-0.3532f, 0.1665f, 0.0f}, 1e-3f);
    expectVec(dash->dmgPivot, {-0.5188f, 0.1674f, 0.0f}, 1e-3f);
    expectVec(dash->wheelPivot, {-0.4208f, 0.2323f, 0.2295f}, 1e-3f);
    expectVec(dash->gearPivotOffset, {0.0222f, 0.0157f, 0.0534f});
}

// --- Screenshot -----------------------------------------------------------------------

namespace {

Mat34 facing(const Vec3& forward, const Vec3& pos) {
    Mat34 m;
    m.m2 = -forward;
    m.m1 = Vec3::yAxis();
    m.m0 = m.m1.cross(m.m2).normalized();
    m.m3 = pos;
    return m;
}

struct Shot {
    GameMode mode = GameMode::Blitz;
    int race = 0;
    int opponents = 0;
    bool dash = false;
    bool countdown = false; // stay at "Ready..." on the start line
    int width = 1280, height = 960;
};

void renderHud(const std::string& out, const Shot& shot) {
    const auto source = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
    ASSERT_TRUE(source);
    const Strings strings = Strings::load(*source);
    auto city = city::loadCity(*test::gameData(), "london");
    ASSERT_TRUE(city);

    RaceConfig cfg;
    cfg.mode = shot.mode;
    cfg.city = "london";
    cfg.raceIndex = shot.race;
    cfg.opponents = shot.opponents;
    std::string error;
    auto session = Session::create(cfg, *city, *test::gameData(), strings, &error);
    ASSERT_TRUE(session) << error;

    PlayerState player;
    player.transform = session->playerSpawn();
    player.speedMph = 63.0f;
    player.rpm = 5400.0f;
    player.maxRpm = 8500.0f;
    player.gear = 3;
    player.damage01 = 0.35f;
    session->start();
    session->update(1.0f / 30.0f, player);
    if (!shot.countdown) {
        // Drive halfway to the second checkpoint after the countdown.
        for (int i = 0; i < 200 && session->phase() == Phase::Countdown; ++i)
            session->update(1.0f / 30.0f, player);
        const auto& cps = session->checkpoints();
        const Vec3 a = cps[0].position, b = cps[1].position;
        for (int i = 1; i <= 60; ++i) {
            const Vec3 p = lerp(a, b, static_cast<float>(i) / 120.0f);
            player.transform = facing((b - a).normalized(), p);
            session->update(1.0f / 30.0f, player);
        }
    }
    session->takeEvents();

    std::string err;
    ASSERT_TRUE(platform::init({}, &err)) << err;
    render::RendererConfig rc;
    rc.settings.windowWidth = shot.width;
    rc.settings.windowHeight = shot.height;
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
        hud.options().dashboard = shot.dash;
        hud.preload(&art);

        std::vector<MapBlip> blips;
        for (std::size_t i = 0; i < session->opponents().size(); ++i)
            blips.push_back({session->opponents()[i].spawn, MapBlip::Kind::Opponent});

        Camera cam;
        const Vec3 fwd = -player.transform.m2;
        if (shot.dash) {
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
            const float aspect = static_cast<float>(ext.width) / static_cast<float>(ext.height);
            fc.proj = Mat44::perspective(0.9f, aspect, 0.1f, 1000.0f, true);
            fc.cameraPosition = cam.position();
            fc.lights[0] = {Vec3{0.3f, -1.0f, -0.4f}.normalized(), {1, 1, 1}};
            fc.ambient = {0.5f, 0.5f, 0.5f};
            dev.setFrameConstants(fc);
            hud.drawWorld(*session, cam, player, 0.3f, blips);
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
    renderHud(stem + "_blitz.png", {.mode = GameMode::Blitz, .width = 1280, .height = 720});
    renderHud(stem + "_circuit.png", {.mode = GameMode::Circuit, .opponents = 5});
    renderHud(stem + "_dash.png",
              {.mode = GameMode::Checkpoint, .opponents = 3, .dash = true, .width = 1280, .height = 720});
    renderHud(stem + "_start.png",
              {.mode = GameMode::Checkpoint, .opponents = 5, .countdown = true, .width = 640, .height = 480});
}
