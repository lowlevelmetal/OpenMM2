#pragma once

// Global physics constants and where their values come from (the code of
// midtown2.exe build 3393, MM2Recomp; see docs/physics.md).

namespace mm2::phys {

// Gravity (m/s^2, applied along -Y). dgPhysEntity::Update adds Mass times
// MM2's gravity global (-19.6, never changed) to the force of every physics
// entity (cars, trailers, traffic that left its rail, props) before it
// integrates. (MM1 used 19.8.)
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

// vehCarSim::Init: the car body's angular velocity limit, 4 pi per axis.
inline constexpr float kCarMaxAngVelocity = 12.566371f;

// ?MetricFactor@@3MA: the game shows |velocity . car axis| * 2.2360249 (mph;
// the factor is never changed).
inline constexpr float kMetersPerSecondToMph = 2.2360249f;

} // namespace mm2::phys
