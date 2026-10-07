#pragma once

// Point-of-view camera: the Angel engine's PovCamCS (Midtown Madness 1;
// Open1560, GPL-3.0, Copyright (C) Brick). Used for the hood view
// (<car>.campovcs) and the dashboard view (<car>_dash.campovcs).

#include "game/CamCar.h"

#include <cstdint>

namespace mm2::game {

class PovCamera final : public CarCamera {
public:
    // `dash`: the dashboard camera (PovCamCS+0x140 set by mmPlayer::Init),
    // which shows the dash model instead of hiding the car.
    explicit PovCamera(const PovCamParams& params = {}, bool dash = false);

    const PovCamParams& params() const { return m_params; }
    void setParams(const PovCamParams& params);
    bool isDash() const { return m_dash; }

    void reset(const CameraTarget& target) override;
    void update(float dt, const CameraTarget& target, const CameraProbe& probe, const CameraInput& input) override;
    CarDisplay display() const override { return m_dash ? CarDisplay::Dash : CarDisplay::Hidden; }

    struct Options {
        // MM2 ReverseOffset: move the eye there while looking back (inferred).
        bool reverseOffset = true;
    };
    Options options;

    // Seed of the shake jitter (the original used the global frand()).
    void setRandomSeed(std::uint32_t seed) { m_random = seed ? seed : 1u; }

private:
    void updatePov(float dt, const CameraTarget& t, const CameraInput& input);
    float frand(); // uniform [0, 1)

    PovCamParams m_params;
    bool m_dash;
    Vec3 m_resetPosition; // 0x128: car position at the last reset
    std::uint32_t m_random = 0x12345678u;
};

} // namespace mm2::game
