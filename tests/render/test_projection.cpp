#include "render/Projection.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::render;

namespace {
float hfov(const ProjectionParams& p) { return 2.0f * std::atan(std::tan(p.fovY * 0.5f) * p.aspect); }
constexpr float kFov = 70.0f * kDegToRad; // tune/camera/*.campovcs CameraFOV
} // namespace

TEST(Projection, FourByThreeMatchesOriginalInEveryMode) {
    for (FovMode m : {FovMode::HorPlus, FovMode::VertMinus, FovMode::Stretch}) {
        const ProjectionParams p = computeProjection(kFov, 4.0f / 3.0f, m);
        EXPECT_NEAR(hfov(p), kFov, 1e-5f) << fovModeName(m);
        EXPECT_NEAR(p.aspect, 4.0f / 3.0f, 1e-6f);
    }
}

TEST(Projection, HorPlusKeepsVerticalFov) {
    const ProjectionParams p43 = computeProjection(kFov, 4.0f / 3.0f, FovMode::HorPlus);
    const ProjectionParams p169 = computeProjection(kFov, 16.0f / 9.0f, FovMode::HorPlus);
    const ProjectionParams p219 = computeProjection(kFov, 21.0f / 9.0f, FovMode::HorPlus);
    EXPECT_NEAR(p43.fovY, p169.fovY, 1e-6f);
    EXPECT_NEAR(p43.fovY, p219.fovY, 1e-6f);
    EXPECT_GT(hfov(p169), kFov);
    EXPECT_GT(hfov(p219), hfov(p169));
    // Narrower than 4:3 (portrait, 5:4) also keeps the vertical FOV.
    EXPECT_NEAR(computeProjection(kFov, 1.25f, FovMode::HorPlus).fovY, p43.fovY, 1e-6f);
}

TEST(Projection, HorPlusMaxAspectLimitsUltrawide) {
    const float limit = 16.0f / 9.0f;
    const ProjectionParams p169 = computeProjection(kFov, limit, FovMode::HorPlus, limit);
    const ProjectionParams p329 = computeProjection(kFov, 32.0f / 9.0f, FovMode::HorPlus, limit);
    EXPECT_NEAR(hfov(p329), hfov(p169), 1e-5f);
    EXPECT_LT(p329.fovY, p169.fovY);
}

TEST(Projection, VertMinusKeepsHorizontalFov) {
    const ProjectionParams p = computeProjection(kFov, 16.0f / 9.0f, FovMode::VertMinus);
    EXPECT_NEAR(hfov(p), kFov, 1e-5f);
}

TEST(Projection, StretchUsesOriginalAspect) {
    const ProjectionParams p = computeProjection(kFov, 16.0f / 9.0f, FovMode::Stretch);
    EXPECT_FLOAT_EQ(p.aspect, 4.0f / 3.0f);
}

TEST(UiLayout, FitLetterboxesWideScreens) {
    const UiLayout l = computeUiLayout({1920, 1080}, UiScaleMode::Fit);
    EXPECT_FLOAT_EQ(l.scaleX, 2.25f);
    EXPECT_FLOAT_EQ(l.scaleY, 2.25f);
    EXPECT_FLOAT_EQ(l.offsetX, 240.0f);
    EXPECT_FLOAT_EQ(l.offsetY, 0.0f);
    EXPECT_EQ(l.safeRect.width, 1440u);
    EXPECT_EQ(l.safeRect.height, 1080u);
    EXPECT_LT(l.left, 0.0f);
    EXPECT_GT(l.right, 640.0f);
    EXPECT_NEAR(l.right - l.left, 1920.0f / 2.25f, 1e-3f);
    const Vec2 p = l.toPixels({640, 480});
    EXPECT_FLOAT_EQ(p.x, 1680.0f);
    EXPECT_FLOAT_EQ(p.y, 1080.0f);
    const Vec2 v = l.toVirtual(p);
    EXPECT_NEAR(v.x, 640.0f, 1e-4f);
}

TEST(UiLayout, FitPillarboxesTallScreens) {
    const UiLayout l = computeUiLayout({1280, 1024}, UiScaleMode::Fit);
    EXPECT_FLOAT_EQ(l.scaleX, 2.0f);
    EXPECT_FLOAT_EQ(l.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(l.offsetY, 32.0f);
}

TEST(UiLayout, StretchFillsWindow) {
    const UiLayout l = computeUiLayout({1920, 1080}, UiScaleMode::Stretch);
    EXPECT_FLOAT_EQ(l.scaleX, 3.0f);
    EXPECT_FLOAT_EQ(l.scaleY, 2.25f);
    EXPECT_FLOAT_EQ(l.left, 0.0f);
    EXPECT_FLOAT_EQ(l.right, 640.0f);
}

TEST(UiLayout, IntegerScaleIsWholeNumber) {
    const UiLayout l = computeUiLayout({1920, 1080}, UiScaleMode::Integer);
    EXPECT_FLOAT_EQ(l.scaleX, 2.0f);
    EXPECT_FLOAT_EQ(l.offsetX, 320.0f);
    EXPECT_FLOAT_EQ(l.offsetY, 60.0f);
    // Smaller than 640x480 falls back to fractional scaling.
    const UiLayout small = computeUiLayout({320, 240}, UiScaleMode::Integer);
    EXPECT_FLOAT_EQ(small.scaleX, 0.5f);
}

TEST(RenderScale, ScaledExtent) {
    EXPECT_EQ(scaledExtent({1920, 1080}, 1.0f), (Extent2D{1920, 1080}));
    EXPECT_EQ(scaledExtent({1920, 1080}, 0.5f), (Extent2D{960, 540}));
    EXPECT_EQ(scaledExtent({1280, 720}, 1.5f), (Extent2D{1920, 1080}));
    EXPECT_EQ(scaledExtent({1, 1}, 0.25f), (Extent2D{1, 1}));
    EXPECT_EQ(scaledExtent({1000, 1000}, 10.0f), (Extent2D{2000, 2000})); // clamped to 2x
}

TEST(Matrices, PerspectiveMapsNearAndFarToZeroAndOne) {
    const Mat44 p = Mat44::perspective(1.0f, 1.5f, 0.5f, 100.0f, true);
    const Vec4 n = p.transform({0, 0, -0.5f, 1});
    const Vec4 f = p.transform({0, 0, -100.0f, 1});
    EXPECT_NEAR(n.z / n.w, 0.0f, 1e-6f);
    EXPECT_NEAR(f.z / f.w, 1.0f, 1e-5f);
}

TEST(Matrices, OverlayOrthoMapsPixelsToClip) {
    // Mirrors overlayConstants(): pixel (0,0) is the top-left corner (y up in clip space).
    const Mat44 o = Mat44::orthographic(0, 800, 600, 0, -1, 1, true);
    const Vec4 tl = o.transform({0, 0, 0, 1});
    const Vec4 br = o.transform({800, 600, 0, 1});
    EXPECT_NEAR(tl.x, -1.0f, 1e-6f);
    EXPECT_NEAR(tl.y, 1.0f, 1e-6f);
    EXPECT_NEAR(br.x, 1.0f, 1e-6f);
    EXPECT_NEAR(br.y, -1.0f, 1e-6f);
}
