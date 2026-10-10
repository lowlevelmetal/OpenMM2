#pragma once

// The players' cars in a network race, simulated by the host (OpenMM2; see
// docs/multiplayer.md, "Players' cars", and net/PlayerCars.h for the wire).
//
// MM2 ran a network car on every machine as a vehCar pulled toward its
// owner's packets (mmNetObject::PositionUpdate, Predict), so each machine
// had its own version of every collision. OpenMM2's host simulates every
// player's car from the players' inputs instead: collisions between
// players, with the traffic and with the props happen once, on the host.
//
//   NetCarDriver     what a machine does to a car each sample: the commands
//                    due (resets), then the input (mmGame::
//                    UpdateSteeringBrakes, the gearbox keys, the hold, the
//                    gold's mass), the same on the host and on the car's
//                    own machine
//   HostInputQueue   host: one client's numbered inputs, applied one per
//                    sample; a late or missing one is repeated, then the
//                    car coasts
//   CarPrediction    client: its own car runs ahead on its inputs at once
//                    (no latency); each host state for an earlier sample
//                    that differs from what was predicted puts the car back
//                    there and runs the later inputs again
//   CorrectionBlend  client: what a correction moved, drawn away over a
//                    few frames (a large one at once)
//
// Nothing here touches the network or the race's presentation.

#include "game/PlayerVehicle.h"
#include "net/PlayerCars.h"
#include "net/Snapshot.h"
#include "phys/World.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace mm2::game {

// --- Inputs --------------------------------------------------------------------------

// The pedals as recorded (bytes, controls::replayQuantize) <-> an input frame.
net::CarInputFrame inputFrame(const phys::PedalInput& recorded);
phys::PedalInput pedalsOf(const net::CarInputFrame& frame);

class NetCarDriver {
public:
    // The car's mass without gold (call once the car is loaded).
    void attach(const SimVehicle& car);
    // One sample's input, before the sample: the gearbox switches and keys
    // (mmGame::UpdateGameInput's TRANSMISSION, SHIFT UP / DOWN and REVERSE),
    // the gold's mass (mmMultiCR::FondleCarMass), then the pedals: after the
    // finish the car brakes with the wheel turned (mmPlayer +0x2258); held on
    // the grid (vehCar::SetDrivable(0, 1)) the brakes are on; otherwise
    // mmGame::UpdateSteeringBrakes (the throttle capped in a forward gear by
    // the gold, then ArcadeControls' automatic reverse).
    // With net::kInputRegen first mmPlayer::UpdateRegen (OpenMM2: once a
    // sample, MM2 once a frame); returns true when that cleared the car's
    // damage (mmPlayer::ResetDamage).
    bool apply(SimVehicle& car, const net::CarInputFrame& in);
    // A reset the client's rules asked for (net::CarCommand).
    static void command(SimVehicle& car, const net::CarCommand& c);

    std::uint16_t extraMass() const { return m_extraMass; }

private:
    float m_baseMass = 0.0f;
    std::uint16_t m_extraMass = 0;
};

// --- The host ------------------------------------------------------------------------

class HostInputQueue {
public:
    // Inputs a sample may be ahead of the one applied (further ones are
    // ignored), and the inputs in hand the queue waits for before its first
    // (the margin it keeps against late ones; the client's pace keeps it).
    static constexpr std::uint32_t kMaxAhead = 240;
    static constexpr std::uint32_t kStartMargin = 3;
    // A queue that had more than this many inputs in hand at every sample
    // for a second (a client whose inputs piled up while the host was busy
    // loading, or a client that stalled and caught up) drops all but the
    // newest few: its car would otherwise lag its player by that much.
    static constexpr std::int32_t kMaxSlack = 12;
    static constexpr int kSlackSamples = 60;
    // Samples a missing input repeats the last one before the car coasts
    // (no throttle, brake or steering).
    static constexpr int kRepeatSamples = 15;

    // A client's message (untrusted: frames far from the next sample are
    // ignored, commands already applied are dropped).
    void receive(const net::PlayerInputMsg& msg);

    struct Next {
        net::CarInputFrame frame;
        std::vector<net::CarCommand> commands; // due at this sample (or late)
        std::uint32_t seq = 0;
        bool real = false; // the client's own input for this sample
    };
    // The next sample's input, or nothing before the first message.
    std::optional<Next> next();

    bool started() const { return m_next != 0; }
    // Enough inputs in hand to begin with (the host places the car then).
    bool ready() const { return m_next != 0 && m_newest + 1 >= m_next + kStartMargin; }
    // The first command that places the car (ResetTo or RespawnAt) among
    // those waiting: the host puts the car in the world with it.
    std::optional<net::CarCommand> placement() const;
    // The last input applied (CarStatesMsg::ack).
    std::uint32_t lastApplied() const { return m_next == 0 ? 0 : m_next - 1; }
    // The fewest inputs waiting behind the applied one since the last call
    // (negative: samples the queue ran short); for CarStatesMsg::waiting.
    std::int32_t takeLeastWaiting();
    // Inputs applied that the client did not send in time (repeated or
    // coasted), and inputs dropped to catch up (kMaxSlack), since the start.
    std::uint64_t missed() const { return m_missed; }
    std::uint64_t skipped() const { return m_skipped; }

private:
    std::map<std::uint32_t, net::CarInputFrame> m_frames;
    std::map<std::uint32_t, net::CarCommand> m_commands;
    std::uint32_t m_next = 0; // the sample the next input is for
    std::uint32_t m_newest = 0;
    std::uint32_t m_lastCommand = 0;
    net::CarInputFrame m_last;
    int m_starved = 0;
    std::int32_t m_leastWaiting = 1 << 20;
    std::int32_t m_slackLeast = 1 << 20; // the fewest in hand over the current second
    int m_slackSamples = 0;
    std::uint64_t m_missed = 0, m_skipped = 0;
};

// Whether the host carries out a client's reset of its car (OpenMM2: the
// game resets a network player's car only where its rules do, and the host
// checks them on its own simulation of the car; a hostile client would
// teleport, or repair its car at will):
// * ResetTo puts the car at its start: only the first command, which
//   places it, in the city;
// * Reset and RespawnAt (mmPlayer::Reset from mmGameMulti::HitWaterHandler,
//   and DropThruCityHandler, which multiplayer treats as the water): the car
//   has been in the water (its vehSplash latched) for nearly
//   HitWaterHandler's 5 s, or fell below mmGame::Update's -50 m (the client's
//   rules see its own car a little ahead of the host's: a second's and 10 m's
//   slack); RespawnAt (a race with checkpoints) only at one of the race's
//   checkpoints, facing its heading (Session's respawn there); not more than
//   four times a second;
// * ClearDamage (vehCar::ClearDamage): the wreck penalty's repair (the car is
//   past its maximum damage), or Cops and Robbers' repair at a delivery of the
//   gold (mmMultiCR's UpdateBank / UpdateHideout: allowed in that mode until
//   its rules run on the host).
// `debug` (OPENMM2_DEBUG_RESPAWN_MS on the host, a development aid) lets any
// reset in the city pass.
struct ResetRules {
    bool placed = false;           // the car is in the race (its first command carried out)
    std::uint32_t lastMoveSeq = 0; // the sample of the last command that moved it
    int waterSamples = 0;          // the samples it has been in the water
    float height = 0.0f;           // its centre of mass's y
    bool wrecked = false;          // past its maximum damage (mmPlayer::IsMaxDamaged)
    bool copsAndRobbers = false;
    bool debug = false;
    Aabb city;                          // the city's bounds (200 m round them allowed)
    std::span<const Mat34> respawnPoints; // the race's checkpoints' spawns (game::session::spawnAt)

    static constexpr std::uint32_t kMoveInterval = 15; // samples
    static constexpr int kWaterSamples = 4 * 60;      // HitWaterHandler's 5 s less a second
    static constexpr float kDropHeight = -50.0f + 10.0f;
    bool allows(const net::CarCommand& c) const;
};

// The car's state as the host sends it to its player, and the car put there
// (the rest of its state stays as it was).
net::OwnCarState ownCarState(const SimVehicle& car, std::uint32_t resets);
void applyOwnCarState(SimVehicle& car, const net::OwnCarState& state);

// A player's car as the others draw it.
net::VehicleSnapshot carSnapshot(const SimVehicle& car, const net::CarInputFrame& input);

// --- The client ----------------------------------------------------------------------

class CarPrediction {
public:
    struct Options {
        std::size_t history = 180;  // samples kept (3 s)
        std::size_t maxReplay = 120; // samples run again at most (a later state corrects instead)
        // What counts as the host's state differing from the prediction.
        float positionTolerance = 0.003f;  // m
        float velocityTolerance = 0.03f;   // m/s
        float rotationTolerance = 0.0015f; // matrix entries
        // Other players' cars run with this one (Companion) farther than
        // this from it, and than their speeds close in the samples run
        // again, run again without it (m; two cars' half lengths and a
        // margin).
        float companionReach = 8.0f;
    };
    CarPrediction() = default;
    explicit CarPrediction(const Options& o) : m_options(o) {}

    // The number the next sample gets (from 1).
    std::uint32_t nextSeq() const { return m_next; }
    std::uint32_t acknowledged() const { return m_acked; }

    // A reset for the next sample. It is applied to the car now (the car
    // stands as it will at that sample's start) and again whenever the
    // samples are run again.
    void command(SimVehicle& car, net::CarCommand c);
    // Around each sample: its input (applied and kept), then the car's state.
    void beginSample(SimVehicle& car, NetCarDriver& driver, const net::CarInputFrame& input);
    void endSample(const SimVehicle& car, const NetCarDriver& driver);

    // The inputs and commands the host has not acknowledged (the newest
    // net::kMaxInputFrames), or nothing before the first sample.
    std::optional<net::PlayerInputMsg> message() const;

    struct Correction {
        bool corrected = false;
        int replayed = 0;
        float positionError = 0.0f; // at the acknowledged sample
        float velocityError = 0.0f;
        float rotationError = 0.0f;  // the largest difference of the matrices' entries
        bool damage = false, held = false, gear = false; // which of these differed
        Vec3 moved; // where the car is now against where it was predicted
        bool rebased = false; // other cars were put to the host's state with it (companions)
    };
    // Another player's car the client simulates along with its own (the
    // host sent it in full: net::NearCarState): put to the host's state at
    // the acknowledged sample and run with it on the input the host last
    // applied to it.
    struct Companion {
        SimVehicle* car = nullptr;
        NetCarDriver* driver = nullptr;
        const net::OwnCarState* state = nullptr;
        net::CarInputFrame input;
        // It comes before the car in the world's movers (a lower player
        // number: every machine keeps the players' cars in that order), and
        // collides first when they run again, as on the host.
        bool first = false;
    };
    // The host's state after sample `ack`. Forgets what it acknowledged; when
    // the state differs from the prediction for that sample, puts the car
    // there and runs the later samples again on their inputs (alone:
    // phys::World::replaySample), calling `beforeEach` with each sample's
    // number before it (the other players' cars put back where they stood
    // when it first ran) and `beforeLast` before the last one (the drawing
    // keeps the car's pose before its last sample). With `companions` the
    // samples are run again whether the car's state differed or not, every
    // companion with it from the host's state (the car's own state at `ack`
    // is the host's only when it differed).
    Correction acknowledge(SimVehicle& car, NetCarDriver& driver, phys::World& world, std::uint32_t ack,
                           const net::OwnCarState& host, const std::function<void()>& beforeLast = {},
                           const std::function<void(std::uint32_t)>& beforeEach = {},
                           std::span<const Companion> companions = {});

    // Statistics since the start.
    struct Stats {
        std::uint64_t acks = 0, corrections = 0, replayedSamples = 0, unreplayable = 0;
    };
    const Stats& stats() const { return m_stats; }

private:
    struct Entry {
        std::uint32_t seq = 0;
        net::CarInputFrame input;
        std::vector<net::CarCommand> commands; // applied before the input
        // The state after the sample.
        phys::CarSimState car;
        std::optional<phys::TrailerState> trailer;
        phys::ArcadeControls controls;
        bool held = false;
        NetCarDriver driver;
    };
    void save(Entry& e, const SimVehicle& car, const NetCarDriver& driver) const;
    static void restore(const Entry& e, SimVehicle& car, NetCarDriver& driver);

    Options m_options;
    std::deque<Entry> m_history; // oldest first, consecutive numbers
    std::vector<net::CarCommand> m_pending; // for the next sample
    std::vector<net::CarCommand> m_unacked;
    std::uint32_t m_next = 1;
    std::uint32_t m_acked = 0;
    Stats m_stats;
};

// What a correction moved the car by, drawn away over a few frames: the
// drawn car stays where it was and eases onto the corrected one; a jump
// beyond `snapDistance` (a reset) is drawn at once.
class CorrectionBlend {
public:
    float halfLife = 0.06f;    // s
    float snapDistance = 4.0f; // m
    // The car was drawn at `before` (apply() of the drawing before the
    // correction) and now would be at `after`.
    void add(const Mat34& before, const Mat34& after);
    void update(float dt);
    void clear();
    Mat34 apply(const Mat34& drawn) const;
    float offset() const { return m_position.mag(); }

private:
    Vec3 m_position;
    Mat34 m_rotation = Mat34::identity(); // drawn = m_rotation x the car's
    bool m_active = false;
};

} // namespace mm2::game
