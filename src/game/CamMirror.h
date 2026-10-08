#pragma once

// The rear-view mirror's camera: Midtown Madness 2's mmMirror (MM2Recomp,
// build 3393), the camera side only. Drawing it is the renderer's job; see
// docs/camera.md ("Rear-view mirror") for what that takes.
//
// mmViewMgr::Init creates the mirror; mmGameManager::Update declares it for
// drawing every frame while its node is active, whatever the camera, as a
// small inset at the top right of the screen (after the dashboard and the
// HUD map). It starts on or off with the driver's view settings
// (mmPlayerConfig) and the "mirror" control toggles it
// (mmViewMgr::SetViewSetting(9)).

#include "core/Math.h"
#include "data/DatFile.h"

#include <optional>
#include <string>
#include <string_view>

namespace mm2::vfs {
class Vfs;
}

namespace mm2::game {

// tune/<car>.mmmirror (mmMirror::FileIO). Defaults are the constructor's,
// except Position, which mmMirror::Init sets before loading the file.
struct MirrorParams {
    Vec3 position{0.0f, 1.4f, -1.0f}; // Position: the eye in the car's frame (1.4 m up, 1 m ahead)
    Vec2 size{0.3f, 0.16f};           // Size: inset width and height as fractions of the screen
    float fov = 10.0f;                // Fov: vertical field of view, degrees (gfxViewport::Perspective)
    float aspect = 2.0f;              // Aspect: fixed projection aspect, whatever the inset's shape
    float nearClip = 1.2f;            // NearClip
    float farClip = 100.0f;           // FarClip

    // Reads the fields present in `node`.
    void load(const data::DatNode& node);
};

// Reads tune/<car>.mmmirror. Returns std::nullopt when the file is missing
// or malformed (only 11 of the cars ship one; the others keep the defaults,
// as asNode::Load leaves them).
std::optional<MirrorParams> loadMirrorParams(const vfs::Vfs& vfs, std::string_view car,
                                             std::string* error = nullptr);

class RearViewMirror {
public:
    RearViewMirror();

    // mmMirror::Init: the defaults, then the car's file.
    void load(const vfs::Vfs& vfs, std::string_view car);
    const MirrorParams& params() const { return m_params; }
    void setParams(const MirrorParams& params);

    // The mirror node's active flag: mmViewMgr::Init clears it when the
    // driver's view settings have the mirror off; SetViewSetting(9) flips it.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }
    void toggle() { m_enabled = !m_enabled; }

    // The mirror's frame in the car's frame: turned half round about Y, so
    // it looks backwards (-m2 is the car's +Z), then m0 negated, which
    // mirrors the picture left to right. Its determinant is -1, so the
    // renderer must swap the front-face winding while drawing it
    // (mmMirror::Cull swaps the cull mode).
    const Mat34& localMatrix() const { return m_local; }
    // mmMirror::Cull: the mirror's frame times the car's world matrix
    // (vehCarSim's), the view to render from.
    Mat34 worldMatrix(const Mat34& car) const;

    // mmMirror::Reset: the inset's pixel rectangle, from the top left of a
    // screen of the given size: (Size.x * width) x (Size.y * height), both
    // truncated, one pixel in from the top and the right edge.
    struct Viewport {
        int x = 0, y = 0, width = 0, height = 0;
    };
    Viewport viewport(int screenWidth, int screenHeight) const;

private:
    MirrorParams m_params;
    Mat34 m_local;
    bool m_enabled = true;
};

} // namespace mm2::game
