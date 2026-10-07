#pragma once

// Chase camera: Midtown Madness 2's camTrackCS (MM2Recomp, build 3393).
// See docs/camera.md.

#include "game/CamCar.h"

namespace mm2::game {

class TrackCamera final : public CarCamera {
public:
    explicit TrackCamera(const TrackCamParams& params = {});

    const TrackCamParams& params() const { return m_params; }
    void setParams(const TrackCamParams& params);

    void reset(const CameraTarget& target) override;
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                const CameraPerspective& view) override;

    // Margin kept between the near plane and walls by CollideType 1 (field
    // 0x180, not in the tune file). mmPlayer::Update sets 0.33 for the near
    // and far cameras, or 1.11 while the car's room has flag 0x08.
    void setCollideMargin(float margin) { m_collideMargin = margin; }
    float collideMargin() const { return m_collideMargin; }

    // Debug/test access.
    bool isOnGround() const { return m_isOnGround; }
    bool reverseViewActive() const { return m_reverseView; }
    float swingAngle() const { return m_shared.yRot; } // TrackCamData yaw about the car
    float hillAngle() const { return m_hill; }
    const Vec3& groundNormal() const { return m_groundNormal; }

private:
    // camTrackCS::TrackCamData: approach amount, yaw and pitch about the car.
    struct SharedData {
        float state = 0.0f;
        float approachAmount = 0.0f; // pushes the camera out along its view line
        float yRot = 0.0f;           // swing about the car (reverse view)
        float xRot = 0.0f;
        float unused4 = 0.0f, unused5 = 0.0f;
    };

    void updateCar(float dt, const CameraTarget& t);
    void updateHill(const CameraTarget& t);
    void updateTrack(float dt, const CameraTarget& t);
    void preApproach(float dt, const CameraTarget& t);
    void minMax(const Mat34& previous, const CameraProbe& probe);
    void collide(float dt, Vec3 previousPosition, const CameraProbe& probe, const CameraInput& input,
                 const CameraPerspective& view);

    TrackCamParams m_params;

    // camTrackCS state (field offsets refer to the MM2 layout).
    bool m_matrixTouched = true;    // 0x110: copy goal to camera after the first update
    float m_collideMargin = 0.33f;  // 0x180
    SharedData m_shared;            // 0x184
    float m_inAirTime = 0.0f;       // 0x188
    float m_onGroundTime = 0.0f;    // 0x18C
    bool m_isOnGround = true;       // 0x190
    int m_spinning = 0;             // 0x194 SpinningReallyFast (1, or 2 = forced)
    bool m_frozen = false;          // 0x198 tracking suspended
    float m_hill = 0.0f;            // 0x1B4 pitch for the slope
    Vec3 m_groundNormal;            // 0x228 filtered ground normal
    float m_carSteering = 0.0f;     // 0x23C
    float m_carSpeed = 0.0f;        // 0x240
    bool m_reverseView = false;     // 0x248
    float m_reverseTimer = 0.0f;    // 0x24C
    float m_reverseSign = 1.0f;     // 0x250 which way round the swing goes
    bool m_wasColliding = false;    // 0x258 (CollideType 2)
    float m_collideBaseDist2 = 0.0f; // 0x260
    float m_collideDist2 = 0.0f;     // 0x264
    Vec3 m_desiredPosition;         // 0x268
    Vec3 m_previousDesired;         // 0x274
    Vec3 m_previousTarget;          // 0x280
};

} // namespace mm2::game
