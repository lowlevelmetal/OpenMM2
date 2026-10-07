#include "render/Projection.h"

#include <algorithm>
#include <cmath>

namespace mm2::render {

ProjectionParams computeProjection(float horizontalFov4x3, float viewportAspect, FovMode mode, float maxAspect) {
    if (!(viewportAspect > 0.0f))
        viewportAspect = kOriginalAspect;
    const float horTan4x3 = std::tan(horizontalFov4x3 * 0.5f);
    ProjectionParams p;
    switch (mode) {
    case FovMode::HorPlus: {
        float vertTan = horTan4x3 / kOriginalAspect;
        if (maxAspect > 0.0f && viewportAspect > maxAspect) {
            // Widen only up to maxAspect, then keep that horizontal FOV.
            const float horTanAtMax = vertTan * maxAspect;
            vertTan = horTanAtMax / viewportAspect;
        }
        p.fovY = 2.0f * std::atan(vertTan);
        p.aspect = viewportAspect;
        break;
    }
    case FovMode::VertMinus:
        p.fovY = 2.0f * std::atan(horTan4x3 / viewportAspect);
        p.aspect = viewportAspect;
        break;
    case FovMode::Stretch:
        p.fovY = 2.0f * std::atan(horTan4x3 / kOriginalAspect);
        p.aspect = kOriginalAspect;
        break;
    }
    return p;
}

UiLayout computeUiLayout(Extent2D output, UiScaleMode mode, float vw, float vh) {
    UiLayout l;
    const float w = static_cast<float>(std::max<std::uint32_t>(output.width, 1));
    const float h = static_cast<float>(std::max<std::uint32_t>(output.height, 1));
    switch (mode) {
    case UiScaleMode::Stretch:
        l.scaleX = w / vw;
        l.scaleY = h / vh;
        break;
    case UiScaleMode::Integer: {
        const float fit = std::min(w / vw, h / vh);
        const float s = fit >= 1.0f ? std::floor(fit) : fit;
        l.scaleX = l.scaleY = s;
        break;
    }
    case UiScaleMode::Fit:
        l.scaleX = l.scaleY = std::min(w / vw, h / vh);
        break;
    }
    // Snap the offset to whole pixels so 1:1 UI art stays crisp.
    l.offsetX = std::floor((w - vw * l.scaleX) * 0.5f);
    l.offsetY = std::floor((h - vh * l.scaleY) * 0.5f);
    l.safeRect = {static_cast<std::int32_t>(l.offsetX), static_cast<std::int32_t>(l.offsetY),
                  static_cast<std::uint32_t>(std::lround(vw * l.scaleX)),
                  static_cast<std::uint32_t>(std::lround(vh * l.scaleY))};
    l.left = -l.offsetX / l.scaleX;
    l.top = -l.offsetY / l.scaleY;
    l.right = (w - l.offsetX) / l.scaleX;
    l.bottom = (h - l.offsetY) / l.scaleY;
    return l;
}

Extent2D scaledExtent(Extent2D output, float renderScale) {
    const float s = std::clamp(renderScale, 0.25f, 2.0f);
    return {std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(output.width) * s))),
            std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(output.height) * s)))};
}

} // namespace mm2::render
