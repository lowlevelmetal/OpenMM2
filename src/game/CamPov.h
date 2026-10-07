#pragma once

// Point-of-view camera: Midtown Madness 2's camPovCS (MM2Recomp, build
// 3393). Used for the hood view (<car>.campovcs) and the dashboard view
// (<car>_dash.campovcs).

#include "game/CamCar.h"

namespace mm2::game {

class PovCamera final : public CarCamera {
public:
    // `dash`: the dashboard camera (camPovCS+0x110 set by mmPlayer::Init),
    // which shows the dash model instead of hiding the car.
    explicit PovCamera(const PovCamParams& params = {}, bool dash = false);

    const PovCamParams& params() const { return m_params; }
    void setParams(const PovCamParams& params);
    bool isDash() const { return m_dash; }

    void reset(const CameraTarget& target) override;
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                const CameraPerspective& view) override;
    CarDisplay display() const override { return m_dash ? CarDisplay::Dash : CarDisplay::Hidden; }

    // Look-around angle (field 0x144, radians): mmGame::UpdateGameInput sets
    // it to CamPan * 2 pi on the view's camera while that is a point-of-view
    // camera. 0 leaves the view straight ahead.
    void setPan(float radians) { m_pan = radians; }
    float pan() const { return m_pan; }

    // Reverse mode (field 0x10C == -1): the eye moves to ReverseOffset, the
    // pitch flips and the view turns round. No MM2 code sets it, so
    // ReverseOffset is loaded but has no effect in the game.
    void setReverseMode(bool on) { m_reverse = on; }

private:
    PovCamParams m_params;
    bool m_dash;
    bool m_reverse = false;
    float m_pan = 0.0f;   // 0x144
    Vec3 m_resetPosition; // 0x130: car position at the last reset
};

} // namespace mm2::game
