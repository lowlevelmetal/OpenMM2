#pragma once

// Cameras of the race flow, ported from Midtown Madness 2 (MM2Recomp, build
// 3393):
//   PreCamera   camPreCS    high view behind the car while the race is set up
//   PointCamera camPointCS  fixed point watching the car (post race, water)
//   PolarCamera camPolarCS  orbit round a point, steered with the keyboard
//                           (multiplayer finish line)
// None is loaded from a tune file (camPreCS::Init does not call Load and
// mmPlayer::Init gives the others no name); they run with their constructor
// defaults. See docs/camera.md.

#include "game/CamCar.h"

#include <optional>

namespace mm2::game {

// camPreCS
class PreCamera final : public CarCamera {
public:
    struct Params {
        BaseCamParams base{3.0f, 1.0f, 50.0f, 3.0f, 1600.0f}; // BlendTime 3
        AppCamParams app;
        float polarHeight = 2.0f;    // PolarHeight: added to the height
        float polarDistance = 22.0f; // PolarDistance
        float polarIncline = 1.1f;   // PolarIncline (rad above the horizon)
        float azimuthOffset = 0.0f;  // AzimuthOffset (rad)
    };

    PreCamera();
    Params& params() { return m_params; }

    void reset(const CameraTarget&) override {}
    // camPreCS::MakeActive: face the way the car faces.
    void makeActive(const CameraTarget& target) override;
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                const CameraPerspective& view) override;

private:
    Params m_params;
    float m_azimuth = 0.0f; // 0x118
};

// camPointCS
class PointCamera final : public CarCamera {
public:
    PointCamera();

    // camPointCS::SetPos / SetVel / SetMaxDist / SetMinDist / SetAppRate.
    void setPosition(const Vec3& p);
    void setVelocity(const Vec3& v) { m_velocity = v; }
    void setMaxDist(float d);
    void setMinDist(float d);
    void setAppRate(float rate) { m_appRate = rate; }
    const Vec3& velocity() const { return m_velocity; }

    void reset(const CameraTarget&) override {}
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                const CameraPerspective& view) override;
    // The view takes its perspective from this camera's zoom.
    bool drivesPerspective() const override { return true; }

private:
    BaseCamParams m_baseParams{1.2f, 1.0f, 50.0f, 0.5f, 1600.0f};
    AppCamParams m_appParams;
    Vec3 m_position;        // 0x110
    Vec3 m_velocity;        // 0x11C: drifts the camera; decays as exp(-t / 2)
    float m_maxDist2 = 4900.0f; // 0x128
    float m_maxDist = 70.0f;    // 0x12C: zoom range
    float m_appRate = 0.0f;     // 0x130 (not read)
    float m_minDist = 0.0f;     // 0x134 (not read)
    float m_minDist2 = 0.0f;    // 0x138
};

// camPolarCS: a polar view round its point of interest (the car, or a fixed
// point set with setInterest), written straight to the camera (no
// approach). The keyboard turns, tilts and zooms it every update
// (CameraInput::orbit). mmPlayer keeps three: the two "XCams" of the camera
// cheat (not ported) and the multiplayer finish-line camera
// (mmPlayer::SetMPPostCam).
class PolarCamera final : public CarCamera {
public:
    struct Params {
        BaseCamParams base{1.2f, 1.0f, 50.0f, 0.1f, 1600.0f}; // CameraNear 0.1
        AppCamParams app;
        float polarHeight = 2.5f;    // PolarHeight: added to the height
        float polarDistance = 10.0f; // PolarDistance, kept within 0.5 .. 200
        float polarAzimuth = 2.5f;   // PolarAzimuth (rad)
        float polarIncline = 0.25f;  // PolarIncline (rad above the horizon), kept within +-pi
        float polarDelta = 2.0f;     // PolarDelta: keyboard rate
        int azimuthLock = 0;         // AzimuthLock: azimuth relative to the interest's heading
    };

    PolarCamera();
    Params& params() { return m_params; }

    // The point the camera orbits: the car (nullopt, camCarCS::Init) or a
    // fixed frame (mmPlayer::SetMPPostCam passes the finish line's).
    void setInterest(const std::optional<Mat34>& interest) { m_interest = interest; }

    void reset(const CameraTarget&) override {}
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input,
                const CameraPerspective& view) override;

private:
    Params m_params;
    std::optional<Mat34> m_interest;
};

} // namespace mm2::game
