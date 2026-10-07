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
inline constexpr float kGravity = 19.8f;

// Physics sample step (s) for the deterministic fixed-step driver.
//
// The Angel engine oversamples instead of using a fixed step:
// asOverSample/asSimulation run n = min(floor(frameDelta / SampleStep) + 1,
// MaxSamples) samples of frameDelta / n, and MM1's physics manager calls
// RealTime(35), i.e. SampleStep = 1/35 s, MaxSamples = 20. MM2's
// dgPhysManager has the same SampleStep/MaxSamples fields (values unknown).
// So the original's sub-step varied with the frame rate between SampleStep/2
// and SampleStep. We default to 1/60 s, inside that range, so replays and
// network play are deterministic. World::advanceOversampled() reproduces the
// original scheme.
inline constexpr float kFixedSampleStep = 1.0f / 60.0f;
inline constexpr float kOversampleStep = 1.0f / 35.0f; // MM1 mmPhysicsMGR: OverSample.RealTime(35)
inline constexpr int kOversampleMaxSamples = 20;       // MM1 asOverSample::MaxSamples default

// asInertialCS sleep defaults (MM1 header initialisers: Vel2 = 0.1,
// AngVel2 = 0.1, Time = 1.0).
inline constexpr float kSleepVel2 = 0.1f;
inline constexpr float kSleepAngVel2 = 0.1f;
inline constexpr float kSleepTime = 1.0f;

// mmCarSim::Init: ICS.LimitAngVelocity = 1, ICS.MaxAngVelocity = 4*pi.
inline constexpr float kCarMaxAngVelocity = 12.566371f;

inline constexpr float kMetersPerSecondToMph = 2.2369363f;

} // namespace mm2::phys
