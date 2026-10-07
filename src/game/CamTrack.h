#pragma once

// Chase camera: the Angel engine's TrackCamCS (Midtown Madness 1; Open1560,
// GPL-3.0, Copyright (C) Brick), plus inferred handling for the MM2-only tune
// fields (hills, reverse view, look around). See docs/camera.md for what is
// ported and what is inferred.

#include "game/CamCar.h"

namespace mm2::game {

class TrackCamera final : public CarCamera {
public:
    explicit TrackCamera(const TrackCamParams& params = {});

    const TrackCamParams& params() const { return m_params; }
    void setParams(const TrackCamParams& params);

    void reset(const CameraTarget& target) override;
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input) override;
    CarDisplay display() const override { return CarDisplay::Body; }

    // Behaviour switches for the parts that are not in MM1 (docs/camera.md).
    struct Options {
        // Speed-dependent AppXZPos from MinAppXZPos..MaxAppXZPos (Open1560's
        // reconstruction of PreApproach; MM2 tunes these per camera).
        bool preApproach = true;
        // MM2 sets CollideType 1 on every camera; MM1 only knows 2. Treat 1
        // like 2 (pull the camera in front of walls).
        bool collideType1 = true;
        bool hill = true;        // HillMin / HillMax / HillLerp
        bool reverseView = true; // ReverseOn / RevDelay / RevOnApp / RevOffApp
        bool lookAround = true;  // apply CameraInput::camPan to the chase view
    };
    Options options;

    // Debug/test access.
    bool isOnGround() const { return m_isOnGround; }
    bool reverseViewActive() const { return m_reverseView; }
    float steerTarget() const { return m_steerTarget; }
    float hillAngle() const { return m_hill; }

private:
    // TrackCamCS::TrackCamData, shared by a car's cameras in the original.
    struct SharedData {
        int state = 0;
        float approachAmount = 0.0f; // pushes the camera out along its view line
        float yRot = 0.0f;           // swing about the car (look around)
        float xRot = 0.0f;
        int field10 = 0, field14 = 0;
    };

    void updateCar(float dt, const CameraTarget& t);
    void updateHillState(float dt, const CameraTarget& t);
    void applyHill();
    void updateTrack(float dt, const CameraTarget& t);
    void updateLookAndReverse(float dt, const CameraTarget& t, const CameraInput& input);
    void preApproach(float dt, const CameraTarget& t);
    void minMax(const Mat34& previous, const CameraProbe& probe);
    void collide(float dt, Vec3 previousPosition, const CameraProbe& probe);

    TrackCamParams m_params;

    // TrackCamCS state (field offsets refer to the MM1 layout).
    bool m_matrixTouched = true;  // 0x118: copy goal to camera after the first update
    SharedData m_shared;          // 0x170
    float m_inAirTime = 0.0f;     // 0x174
    float m_onGroundTime = 0.0f;  // 0x178
    bool m_isOnGround = true;     // 0x17C
    int m_spinning = 0;           // 0x180 SpinningReallyFast (1, or 2 = forced)
    bool m_frozen = false;        // 0x184 tracking suspended
    int m_splineState1 = 0;       // 0x188 } swing transitions; nothing in MM1
    int m_splineState3 = 0;       // 0x190 } sets them, see docs/camera.md
    float m_xRotBase = 0.0f;      // 0x1A0
    float m_steerTimer = 0.0f;    // 0x228 (never advanced in MM1, so DriftDelay has no effect)
    float m_carSteering = 0.0f;   // 0x230
    float m_carVelocity = 0.0f;   // 0x234
    float m_steerTarget = 0.0f;   // 0x238
    bool m_wasColliding = false;  // 0x240
    float m_collideBaseDist2 = 0.0f; // 0x248
    float m_collideDist2 = 0.0f;     // 0x24C
    Vec3 m_desiredPosition;       // 0x250
    Vec3 m_previousDesired;       // 0x25C
    Vec3 m_previousTarget;        // 0x268

    // MM2 inferred state.
    float m_hill = 0.0f;
    float m_reverseTimer = 0.0f;
    bool m_reverseView = false;
};

} // namespace mm2::game
