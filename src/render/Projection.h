#pragma once

#include "render/DisplaySettings.h"
#include "render/Types.h"

namespace mm2::render {

// The original user interface and camera were authored for 640x480 (4:3).
inline constexpr float kOriginalWidth = 640.0f;
inline constexpr float kOriginalHeight = 480.0f;
inline constexpr float kOriginalAspect = kOriginalWidth / kOriginalHeight;

struct ProjectionParams {
    float fovY = 1.0f;   // vertical field of view, radians
    float aspect = 1.0f; // aspect ratio to build the projection with
};

// Angel cameras specify a horizontal FOV intended for a 4:3 screen
// (asCamera::SetView(horz_fov, aspect, ...); see tune/camera/*.campovcs
// "CameraFOV"). Returns the vertical FOV and projection aspect to use for a
// viewport of `viewportAspect` (width / height):
//   HorPlus   - vertical FOV of the 4:3 original; wider screens see more at
//               the sides. With maxAspect > 0, screens wider than maxAspect
//               are cropped vertically instead (ultrawide comfort limit).
//   VertMinus - horizontal FOV kept; wider screens see less vertically.
//   Stretch   - the 4:3 image is stretched to the viewport.
ProjectionParams computeProjection(float horizontalFov4x3, float viewportAspect, FovMode mode,
                                   float maxAspect = 0.0f);

// Maps the 640x480 virtual UI space onto an output of arbitrary size.
struct UiLayout {
    float scaleX = 1.0f, scaleY = 1.0f;   // virtual units -> pixels
    float offsetX = 0.0f, offsetY = 0.0f; // pixel position of virtual (0,0)
    Rect safeRect;                        // the 640x480 area, in pixels
    // Virtual-space bounds of the whole output. With Fit on a 16:9 screen,
    // left < 0 and right > 640: HUD elements anchored to screen edges can be
    // placed at these coordinates instead of the 4:3 edges.
    float left = 0.0f, top = 0.0f, right = kOriginalWidth, bottom = kOriginalHeight;

    Vec2 toPixels(Vec2 v) const { return {offsetX + v.x * scaleX, offsetY + v.y * scaleY}; }
    Vec2 toVirtual(Vec2 p) const { return {(p.x - offsetX) / scaleX, (p.y - offsetY) / scaleY}; }
};

UiLayout computeUiLayout(Extent2D output, UiScaleMode mode, float virtualWidth = kOriginalWidth,
                         float virtualHeight = kOriginalHeight);

// Size of the 3D render target for an output size and render scale.
Extent2D scaledExtent(Extent2D output, float renderScale);

} // namespace mm2::render
