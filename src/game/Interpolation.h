#pragma once

// OpenMM2 presentation: what is drawn between two simulation steps.
//
// MM2 ran its whole game once per rendered frame with that frame's measured
// time (datTimeManager::Update sets Seconds to it, clamped; mmGame::Update
// and the AI follow, and dgPhysManager::Update splits the frame into at most
// three samples of at most 1/35 s), so each frame showed the state its own
// update had reached. OpenMM2 instead steps the physics in fixed 1/60 s
// samples (phys::World::advanceFixed) and the AI in fixed 1/30 s steps
// (ai::World::update), so that the simulation is the same at any frame rate
// (docs/physics.md). A frame then runs no step or several, and drawing the
// last step's state made motion judder above 60 fps (and at 60 fps whenever
// a frame ran 0 or 2 samples).
//
// A StepHistory keeps each simulated object's state as it stood when the
// last step began; the drawing blends it towards the object's current state
// by alpha = the time the simulation has not stepped yet / the step. The
// scene is therefore drawn one step behind the frame: up to 16.7 ms more
// latency for what the physics moves, up to 33 ms for the AI's traffic and
// pedestrians. Nothing here feeds back into the simulation.

#include "core/Math.h"
#include "game/VehicleRenderer.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mm2::ai {
class World;
}
namespace mm2::phys {
class World;
}
namespace mm2::game {
class TrafficBodies;
namespace bangers {
class BangerSet;
}
} // namespace mm2::game

namespace mm2::game {

// Two placements blended: the positions lerped, the orientations slerped
// (the short way), each basis row's length (an instance's scale) lerped.
// t <= 0 gives `from` and t >= 1 `to`, exactly.
Mat34 blendTransform(const Mat34& from, const Mat34& to, float t);
// An angle that wraps every `period` (a tyre's turn): the short way round.
float blendAngle(float from, float to, float t, float period);

// How far an object can move in one step before the move counts as a jump
// (a respawn, a reset, a recycled slot): drawn where it is, not blended.
struct JumpLimits {
    float distance = 5.0f; // m: 300 m/s at 60 Hz, 150 m/s at 30 Hz
    float angle = 1.6f;    // rad
};
bool isJump(const Mat34& from, const Mat34& to, const JumpLimits& limits);

// Two vehicle poses blended: the body as blendTransform, the simulated
// wheels in the body's frame (spun by the difference of their accumulated
// turns when both poses carry them, so a wheel turning more than half a
// revolution a step does not turn backwards), the rest-pose wheels' spin,
// steering and drop. The lights, siren and other flags are `to`'s.
VehiclePose blendPose(const VehiclePose& from, const VehiclePose& to, float t);
// The pose moved, wheels and all, so that its body is at `body`.
VehiclePose placePose(const VehiclePose& pose, const Mat34& body);

// What a StepHistory entry is (the high bits of its key).
enum class Drawn : std::uint8_t {
    Player,
    PlayerTrailer,
    Opponent,
    Police,
    RemoteCar,
    RemoteTrailer,
    TrafficCar, // an ambient car the physics has taken over
    Prop,
    RailCar, // an ambient car on its rail (the AI)
    Pedestrian,
};
constexpr std::uint64_t drawnKey(Drawn kind, std::uint64_t id) {
    return (static_cast<std::uint64_t>(kind) << 48) | (id & 0xFFFFFFFFFFFFull);
}

// The states of one stepped simulation's objects as they stood when its
// last step began. The simulation's step observer calls beginStep() and
// record()s each object, at the start of every step; the drawing asks for
// each object's state at the frame's alpha. An object not recorded at the
// start of the last step (new since, or not stepped), recorded under another
// generation (reset, respawned, recycled) or whose move is a jump is drawn
// as it is now.
class StepHistory {
public:
    explicit StepHistory(JumpLimits limits = {}) : m_limits(limits) {}

    // A step begins: what is recorded until the next one is the state
    // before it. Forgets the objects the step before did not record.
    void beginStep();
    void record(std::uint64_t key, const Mat34& transform, std::uint32_t generation = 0, float turn = 0.0f);
    void record(std::uint64_t key, const VehiclePose& pose, std::uint32_t generation = 0);
    // Everything is drawn where it is until the next step (a race reset).
    void clear();

    // The share of a step the simulation has not run yet (its remainder /
    // its step), 0..1.
    void setAlpha(float alpha);
    float alpha() const { return m_alpha; }

    // The drawn state of an object now at `live`.
    Mat34 transform(std::uint64_t key, const Mat34& live, std::uint32_t generation = 0) const;
    // A turn recorded with the transform (a rail car's tyres), wrapping
    // every `period`.
    float turn(std::uint64_t key, const Mat34& live, float liveTurn, float period,
               std::uint32_t generation = 0) const;
    VehiclePose pose(std::uint64_t key, const VehiclePose& live, std::uint32_t generation = 0) const;
    // Whether the object will be blended (it has a usable earlier state).
    bool blends(std::uint64_t key, const Mat34& live, std::uint32_t generation = 0) const;

    std::size_t size() const { return m_transforms.size() + m_poses.size(); }

private:
    struct TransformEntry {
        std::uint64_t step = 0;
        std::uint32_t generation = 0;
        Mat34 transform;
        float turn = 0.0f;
    };
    struct PoseEntry {
        std::uint64_t step = 0;
        std::uint32_t generation = 0;
        VehiclePose pose;
    };
    const TransformEntry* previous(std::uint64_t key, const Mat34& live, std::uint32_t generation) const;
    const PoseEntry* previousPose(std::uint64_t key, const Mat34& live, std::uint32_t generation) const;

    std::unordered_map<std::uint64_t, TransformEntry> m_transforms;
    std::unordered_map<std::uint64_t, PoseEntry> m_poses;
    std::uint64_t m_step = 0;
    float m_alpha = 1.0f;
    JumpLimits m_limits;
};

// ai::World's step observer: its ambient cars on their rails (by id, the
// generation their slot's spawns) and its pedestrians (by id), as they stand
// before the step. The cars the physics has taken over are the physics'.
void recordAiStep(StepHistory& history, const ai::World& world);

// For the physics' step observer: the traffic cars that have a body (by car
// id) and the props an active simulates in the world (by instance), as they
// stand before the sample.
void recordTrafficBodies(StepHistory& history, const ai::World& ai, const TrafficBodies& bodies);
void recordProps(StepHistory& history, const bangers::BangerSet& props, const phys::World& world);
// A traffic car with a body as it is now: its matrix and its wheels
// (aiVehicleActive's vehWheelCheaps); nullopt without a body.
std::optional<VehiclePose> trafficBodyPose(const TrafficBodies& bodies, int carId);

} // namespace mm2::game
