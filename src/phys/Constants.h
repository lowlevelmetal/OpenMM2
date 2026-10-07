#pragma once

// Global physics constants and where their values come from.
//
// Most of the vehicle simulation is ported from Midtown Madness 1 (Open1560's
// game.asm, a symbol-named disassembly of MM1 beta build 1560 on the same Angel
// engine). See docs/physics.md for the evidence level of every value.

namespace mm2::phys {

// Gravity (m/s^2, applied along -Y). Ported from MM1: midtown.cpp calls
// PHYS.SetGravity(-19.8f), mmCarSim::Init copies PHYS.Gravity into
// ICS.Gravity, and mmWheel::Init derives the static wheel load from it.
// The MM2 value is unverified.
// dgPhysEntity::Update: physics entities (vehicles, trailers) fall at
// 19.6 m/s^2 (MM1 used 19.8).
inline constexpr float kGravity = 19.6f;

// Physics sample step (s) for the deterministic fixed-step driver.
//
// MM2's dgPhysManager::Update oversamples each frame: n = min(ceil((frame -
// 0.001) / SampleStep), MaxSamples) samples of frame / n
// (datTimeManager::SetTempOverSampling). The game sets SampleStep = 1/35 s
// and MaxSamples = 3 (mmGame::Init; the manager's own defaults are 1/60 s and
// 6). Below 35 fps a frame is split; at 60 fps it is one 1/60 s sample. The
// fixed-step driver uses 1/60 s, so the simulation behaves as the original
// did at 60 fps whatever the display rate; World::advanceOversampled()
// reproduces the original scheme exactly.
inline constexpr float kFixedSampleStep = 1.0f / 60.0f;
inline constexpr float kOversampleStep = 1.0f / 35.0f; // mmGame::Init: dgPhysManager +0x12ac
inline constexpr int kOversampleMaxSamples = 3;        // mmGame::Init: dgPhysManager +0x12a8

// asInertialCS sleep defaults (MM1 header initialisers: Vel2 = 0.1,
// AngVel2 = 0.1, Time = 1.0).
inline constexpr float kSleepVel2 = 0.1f;
inline constexpr float kSleepAngVel2 = 0.1f;
inline constexpr float kSleepTime = 1.0f;

// vehCarSim::Init: the car body's angular velocity limit, 4 pi per axis.
inline constexpr float kCarMaxAngVelocity = 12.566371f;

// ?MetricFactor@@3MA: the game shows |velocity . car axis| * 2.2360249 (mph;
// the factor is never changed).
inline constexpr float kMetersPerSecondToMph = 2.2360249f;

} // namespace mm2::phys
