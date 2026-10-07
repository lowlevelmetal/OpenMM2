/*
    OpenMM2 - particle birth rules (asBirthRule).
    Field layout first ported from Open1560 (code/midtown/mmeffects/birth.h),
    Copyright (C) 2020 Brick, GPL-3.0-or-later; behaviour follows MM2's own
    asBirthRule (constructor defaults, FileIO, InitSpark).
*/
#pragma once

#include "core/Math.h"
#include "data/DatFile.h"

#include <cstdint>
#include <optional>
#include <string>

namespace mm2::game::fx {

// asBirthRule: how particles are born. Loaded from tune/effects/*.asbirthrule
// ("asBirthRule { ... }"), tune/rain.asbirthrule, the "BirthRule { ... }"
// block of tune/banger/*.dgBangerData and the flat particle fields of
// tune/vehicle/*.vehCarDamage (vehCarDamage::FileIO includes the rule's).
// Defaults are the asBirthRule constructor's.
struct BirthRule {
    Vec3 position, positionVar;
    Vec3 velocity, velocityVar;
    float life = 1.0f, lifeVar = 0.0f;
    float mass = 1.0f, massVar = 0.0f;
    float radius = 1.0f, radiusVar = 0.0f;
    float dRadius = 0.0f, dRadiusVar = 0.0f; // radius growth per 1/60 s
    float drag = 0.0f, dragVar = 0.0f;
    float damp = 1.0f, dampVar = 0.0f; // velocity kept by a bounce (kBounce)
    float spewRate = 0.0f;             // particles per second while spewing
    float spewTimeLimit = 0.0f;        // seconds; 0 = forever
    float gravity = -9.8f;             // vertical acceleration (m/s^2)
    // World height of the plane particles bounce on, stop at or cast their
    // shadow on (kBounce, kStopAtHeight, kShadow).
    float height = 0.0f;
    // Read from the files; asBirthRule::InitSpark and asParticles never use it.
    float intensity = 1.0f;
    // Initial colour as stored in the files: 0xAABBGGRR. InitSpark turns it
    // into the vertex colour 0xAARRGGBB (e.g. splash -331546 = 0xFFFAF0E6 is
    // the pale blue R 230, G 240, B 250).
    std::uint32_t color = 0xFFFFFFFFu;
    int dAlpha = 0, dAlphaVar = 0;          // alpha change per 1/60 s (0..255 units)
    int dRotation = 0, dRotationVar = 0;    // rotation change per 1/60 s (32 units per turn)
    int texFrameStart = 0, texFrameEnd = 0; // frame range in the particle sheet
    int initialBlast = 0;                   // particles emitted at once (wheel particles: per second)
    int birthFlags = 0;

    // BirthFlags bits as MM2's asParticles::Update and asMeshCardInfo::
    // DrawShadows test them (each particle keeps the low byte).
    static constexpr int kStationary = 0x1;   // dgBangerActive: emit in world space, not with the prop
    static constexpr int kBounce = 0x2;       // bounce on `height`, keeping `damp` of the velocity
    static constexpr int kCycleFrames = 0x4;  // advance one frame per update through [start, end)
    static constexpr int kStopAtHeight = 0x8; // stop at `height` and die (rain)
    static constexpr int kShadow = 0x10;      // also draw a flat shadow card on `height`
    // Banger rules also set 0x20 and 0x40 (112 = 0x70); nothing in MM2's
    // particle code reads them.
};

// Reads every known field present in `block`; missing fields keep their
// asBirthRule defaults. Returns false if `block` has none of the fields.
bool loadBirthRule(const data::DatNode& block, BirthRule& out);

// Parses a whole "type: a" file whose top block is an asBirthRule.
std::optional<BirthRule> parseBirthRuleFile(std::string_view text, std::string* error = nullptr);

} // namespace mm2::game::fx
