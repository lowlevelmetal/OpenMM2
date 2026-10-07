/*
    OpenMM2 - particle birth rules (asBirthRule).
    Field layout and InitSpark ported from Open1560
    (code/midtown/mmeffects/birth.h and game.asm asBirthRule::InitSpark),
    Copyright (C) 2020 Brick, GPL-3.0-or-later.
*/
#pragma once

#include "core/Math.h"
#include "data/DatFile.h"

#include <cstdint>
#include <optional>
#include <string>

namespace mm2::game::fx {

// asBirthRule: how particles are born. Loaded from tune/effects/*.asbirthrule
// ("asBirthRule { ... }"), tune/rain.asbirthrule / snow.asbirthrule, the
// "BirthRule { ... }" block of tune/banger/*.dgBangerData and the flat
// particle fields of tune/vehicle/*.vehCarDamage.
struct BirthRule {
    Vec3 position, positionVar;
    Vec3 velocity, velocityVar;
    float life = 1.0f, lifeVar = 0.0f;
    float mass = 1.0f, massVar = 0.0f;
    float radius = 1.0f, radiusVar = 0.0f;
    float dRadius = 0.0f, dRadiusVar = 0.0f; // radius growth per 1/30 s
    float drag = 0.0f, dragVar = 0.0f;
    float damp = 1.0f, dampVar = 0.0f; // velocity multiplier per 1/30 s
    float spewRate = 0.0f;             // particles per second while spewing
    float spewTimeLimit = 0.0f;        // seconds; 0 = forever
    float gravity = -9.8f;             // vertical acceleration (m/s^2)
    int dAlpha = 0, dAlphaVar = 0;         // alpha change per 1/30 s (0..255 units)
    int dRotation = 0, dRotationVar = 0;   // rotation change per 1/30 s (256 units per turn)
    int texFrameStart = 0, texFrameEnd = 0; // frame range in the particle sheet
    int initialBlast = 0;                   // particles emitted at once on start
    int birthFlags = 0;
    // MM2 additions (tune/effects/*.asbirthrule): initial particle colour
    // (packed ARGB; MM1 always started white), and two fields of unknown use.
    std::uint32_t color = 0xFFFFFFFFu;
    float height = 0.0f;
    float intensity = 1.0f;

    // MM1 flag bits (asBirthRule::BirthFlags_).
    static constexpr int kStationary = 0x1;  // emit in world space, not attached to the emitter
    static constexpr int kCycleFrames = 0x4; // animate through the frame range
    static constexpr int kSplashes = 0x8;    // turn into a splash frame when crossing y = 0
    // MM2 data also uses 0x10, 0x20 and 0x40 (bangers: 112 = 0x70); their
    // meaning is unknown and they are ignored.
};

// Reads every known field present in `block`; missing fields keep their
// asBirthRule defaults. Returns false if `block` has none of the fields.
bool loadBirthRule(const data::DatNode& block, BirthRule& out);

// Parses a whole "type: a" file whose top block is an asBirthRule.
std::optional<BirthRule> parseBirthRuleFile(std::string_view text, std::string* error = nullptr);

} // namespace mm2::game::fx
