/*
    OpenMM2 - tyre skid marks (mmSkidManager / mmSkid).
    Laying logic ported from Open1560 (code/midtown/mmcar/skid.cpp and
    game.asm mmSkidManager::LayTrack, mmSkid::AddSkid),
    Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "game/TextureLibrary.h"
#include "render/Device.h"

#include <vector>

namespace mm2::game::fx {

// What one wheel contributes each update.
struct SkidInput {
    float carSpeed = 0.0f;   // m/s
    float wheelSpeed = 0.0f; // |rotation speed * radius|, m/s
    float latSlip = 0.0f;    // vehWheel LatSlipPercent
    float longSlip = 0.0f;   // vehWheel LongSlipPercent
    float slipThreshold = 0.2f; // mmCarSim::SlipPercentThresh (MM1 initialises 0.2, inferred)
    bool onGround = false;
    bool shouldSkid = false; // mmCarSim::ShouldSkid: speed > 7 || throttle > 0.5 || brakes > 0.7
    Vec3 groundNormal{0, 1, 0};
    // Contact frame on the ground: m0 along the axle (track width direction),
    // m3 the contact point.
    Mat34 contact;
    float width = 0.25f;     // tyre width (vehWheel Width)
    bool allowTrack = true;  // false on surfaces that take no marks (MM1: room flag 0x40)
    bool lightMarks = false; // MM1 variant 1: snow weather or physics material type 2
};

// One wheel's ring of skid quads.
class SkidTrail {
public:
    static constexpr float kSpeedThreshold = 3.0f; // SkidSpeedThresh
    static constexpr float kTrackInterval = 0.1f;  // SkidTrackTimeThresh (s)

    explicit SkidTrail(int maxSkids = 64); // MM1: 64 per wheel

    // Returns true while the wheel is skidding (the caller emits particles,
    // as mmWheel::GenerateSkidParticles does).
    bool update(float dt, const SkidInput& in);
    void reset();

    struct Quad {
        Vec3 p[4]; // previous left, previous right, current left, current right
        bool light = false;
        float v0 = 0.0f, v1 = 0.0f; // texture v along the track (OpenMM2)
        bool used = false;
    };
    const std::vector<Quad>& quads() const { return m_quads; }

private:
    void layTrack(const SkidInput& in);

    std::vector<Quad> m_quads;
    int m_next = 0;
    float m_timeSinceTrack = 0.0f;
    bool m_notSkidding = true;
    Vec3 m_prevLeft, m_prevRight;
    float m_distance = 0.0f;
};

// Draws trails with texture/tire_track.tga (MM2 has no MM1-style "skid"
// mesh; the texture is the tread pattern). Texture v advances with the
// distance travelled in tyre widths (inferred).
class SkidRenderer {
public:
    void draw(render::Device& device, TextureLibrary& textures, const std::vector<const SkidTrail*>& trails);
};

} // namespace mm2::game::fx
