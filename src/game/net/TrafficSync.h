#pragma once

// The shared ambient traffic and police of a multiplayer cruise, an OpenMM2
// extra (docs/multiplayer.md, "Shared traffic"; docs/parity/openmm2-only.md).
// MM2's network cruise has no traffic and no police (mmGameMulti::Init); with
// the host's lobby option on, the host runs ai::World's traffic and the
// police for everyone and the clients show what it sends.
//
//   TrafficCatalog  the models a shared car can be, by index (both machines
//                   build it from the same data; a checksum tells them apart)
//   TrafficHost     host side: which cars each client gets (the ones near its
//                   car, the police chasing it first) within a packet budget
//   TrafficClient   client side: the received cars, interpolated in session
//                   time like the remote players (net::SnapshotBuffer)
//
// Nothing here touches the network or the simulation: the race screen feeds
// the host's cars in and sends the messages (game::NetGame), and feeds the
// client's messages in and draws, hears and collides with what comes out.

#include "ai/Traffic.h"
#include "core/Math.h"
#include "game/net/TrafficPrediction.h"
#include "net/AmbientState.h"
#include "net/Snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace mm2::game {

class TrafficCatalog {
public:
    // Adds a model (case-insensitive; duplicates and models beyond
    // net::kMaxAmbientModels are ignored).
    void add(std::string_view model);
    // Index of a model, -1 when it is not listed.
    int find(std::string_view model) const;
    // Model at `index`, null out of range.
    const std::string* name(int index) const;
    std::size_t size() const { return m_models.size(); }
    // FNV-1a over the lower-case names, folded to 16 bits.
    std::uint16_t checksum() const;

private:
    std::vector<std::string> m_models;
};

// One car of the host's simulation as it is shared.
struct SharedCar {
    // 0 .. net::kMaxAmbientIds - 1: the traffic slot, or the police car's
    // place after the pool.
    int id = 0;
    net::AmbientKind kind = net::AmbientKind::Traffic;
    int generation = 0; // changes when the slot is reused (only the low bits travel)
    int model = 0;      // catalog index
    int paint = 0;      // paint job
    Mat34 transform;    // model origin
    float speed = 0.0f; // along -m2 (rail cars)
    RailMotion motion;  // rail cars: acceleration and curvature (RailMotionTracker)
    Vec3 velocity;
    Vec3 angularVelocity;
    std::uint8_t flags = 0; // net::AmbientFlags
    // Its state is a physics body's (a police car, a knocked traffic car):
    // it belongs to the physics step's time, not the AI step's.
    bool body = false;
    // Traffic with a physics body: WHL0-3's drawing offsets from their
    // pivots (trafficWheelOffsets).
    bool wheels = false;
    std::array<Vec3, 4> wheelOffsets{};
    // Police.
    std::uint8_t target = net::kAmbientNoTarget;
    float damage = 0.0f;
    float rpm = 0.0f;
    float throttle = 0.0f;
    int gear = 0;
};

// A client the host sends cars to: its player id and where its car is (the
// host's view of it).
struct TrafficViewer {
    std::uint8_t player = net::kInvalidPlayerId;
    Vec3 position;
};

class TrafficHost {
public:
    struct Options {
        // A car joins a client's set within enterRadius of its car (on the
        // ground plane) and leaves it beyond leaveRadius, so a car near the
        // edge does not come and go with every message.
        float enterRadius = 200.0f;
        float leaveRadius = 230.0f;
        // Beyond this a traffic car on its rail gets its state every other
        // message (the others say only that it is still there); nearer
        // ones, cars off their rails, the police and cars new to the
        // client get it in every message.
        float fullRateRadius = 80.0f;
        // The message's size, bytes (the header included): one unfragmented
        // UDP datagram.
        std::size_t maxBytes = 1100;
    };

    TrafficHost() = default;
    explicit TrafficHost(const Options& options) : m_options(options) {}

    // The message for `viewer`: the police chasing it, then the cars nearest
    // it, as many as fit. Cars with an id or model out of range are skipped.
    net::AmbientStateMsg build(const TrafficViewer& viewer, std::span<const SharedCar> cars,
                               std::uint32_t time, std::uint32_t lightSteps, std::uint16_t catalog);
    // A player left: its set is forgotten.
    void forget(std::uint8_t player) {
        m_sets.erase(player);
        m_sequence.erase(player);
    }

    const Options& options() const { return m_options; }

private:
    Options m_options;
    std::map<std::uint8_t, std::unordered_set<int>> m_sets; // the ids each client got last
    std::map<std::uint8_t, std::uint32_t> m_sequence;       // messages built for each client
};

class TrafficClient {
public:
    struct Options {
        // The least delay a car is shown at when it is interpolated (as the
        // remote players, net::SessionConfig).
        double interpolationDelayMs = 100.0;
        // Beyond its newest message a car is predicted (TrafficPrediction.h)
        // at most this far, then held: a car on its rail, and a body.
        double maxRailPredictionMs = 1000.0;
        double maxBodyPredictionMs = 500.0;
        // A predicted car's drawing blends a newer message's correction away
        // with this time constant; a correction beyond snapDistance (or
        // snapTurn radians) is shown at once.
        double correctionMs = 100.0;
        float snapDistance = 4.0f;
        float snapTurn = 1.0f;
        // A car not heard of for this long is dropped (the host stopped
        // sending, or every message for a while was lost).
        double staleMs = 1500.0;
        // A car that moved this much further than its velocity explains was
        // put somewhere else: it is shown there at once, not blended.
        float teleportDistance = 25.0f;
    };

    // `catalogSize`: the client's own catalog; models beyond it are refused.
    TrafficClient(std::size_t catalogSize, std::uint16_t catalogChecksum)
        : TrafficClient(catalogSize, catalogChecksum, Options{}) {}
    TrafficClient(std::size_t catalogSize, std::uint16_t catalogChecksum, const Options& options);

    // A message from the host (untrusted: checked as it is read).
    void receive(const net::AmbientStateMsg& msg);
    // The cars at `renderTime` (session ms): interpolated between the
    // host's messages while it is behind the newest, predicted beyond it
    // (normally: the time this machine's car is at); cars that have left by
    // then are dropped.
    void update(double renderTime);

    struct Car {
        int id = 0;
        net::AmbientKind kind = net::AmbientKind::Traffic;
        int generation = 0;
        int model = 0;
        int paint = 0;
        Mat34 transform; // model origin
        Vec3 velocity;
        Vec3 angularVelocity;
        float speed = 0.0f;     // along -m2
        std::uint8_t flags = 0; // net::AmbientFlags
        // A knocked car with a body on the host: its wheels' drawing offsets
        // (interpolated between the messages, trafficWheelMatrices).
        bool wheels = false;
        std::array<Vec3, 4> wheelOffsets{};
        std::uint8_t target = net::kAmbientNoTarget;
        float damage = 0.0f;
        float rpm = 0.0f;
        float throttle = 0.0f;
        int gear = 0;
        bool extrapolated = false; // beyond the newest message (predicted)
        bool hornStarted = false;  // the horn sounded since the previous update
        bool fresh = false;        // shown for the first time (or put somewhere new) this update
        std::uint32_t stateTime = 0; // the session time of its newest state from the host
    };
    const std::vector<Car>& cars() const { return m_cars; }
    // The host's traffic light steps at `renderTime`, once a message came.
    std::optional<std::uint32_t> lightSteps(double renderTime) const;
    // OpenMM2 presentation: car `id` (one update() listed) as it was at
    // `time`, up to kDrawBehindMs before update()'s render time: the drawing
    // shows the shared cars a physics step further back, with everything
    // else (game::StepHistory). Before the car's first state, that state. A
    // predicted car is drawn with the correction still being blended away.
    std::optional<Mat34> transformAt(int id, double time) const;
    // Car `id` where the collisions meet it at `time` (predicted from its
    // newest state, or between the messages), without the drawing's
    // correction: for placing the received cars at another time than
    // update()'s, as a client replaying its own car's steps from a host
    // state would.
    std::optional<PredictedPose> poseAt(int id, double time) const;
    // OpenMM2 presentation: car `id` is drawn at `pose` now (a car this
    // machine knocked loose, handed back to the host's messages: game::
    // NetTrafficCars), the difference blended away as a correction is.
    // Returns how far the drawing is from the newest prediction there (0 when
    // unknown).
    float setDrawn(int id, const Mat34& pose);
    static constexpr double kDrawBehindMs = 100.0;
    // The host's catalog differs from this client's.
    bool catalogMismatch() const { return m_mismatch; }
    std::size_t known() const { return m_entries.size(); }
    void clear();

    struct Stats {
        std::uint64_t messages = 0;
        std::uint64_t entities = 0;
        std::uint64_t refused = 0;  // entries with a model out of range, duplicates, bad values
        std::uint64_t outdated = 0; // messages older than the newest
        std::uint64_t teleports = 0;
        std::uint64_t snaps = 0; // predicted cars whose correction was shown at once
    };
    const Stats& stats() const { return m_stats; }
    const Options& options() const { return m_options; }

private:
    // A knocked car's wheels as one message gave them.
    struct WheelSample {
        std::uint32_t time = 0;
        bool wheels = false;
        std::array<Vec3, 4> offsets{};
    };
    struct Entry {
        int generation = 0;
        net::AmbientKind kind = net::AmbientKind::Traffic;
        int model = 0;
        int paint = 0;
        net::SnapshotBuffer buffer{32};
        std::deque<WheelSample> wheels; // time order, newest last
        std::uint32_t firstTime = 0; // its first snapshot (hidden before it)
        std::uint32_t lastTime = 0;  // its newest snapshot
        std::optional<std::uint32_t> goneAt; // missing from a newer message: gone from this time on
        std::uint8_t target = net::kAmbientNoTarget;
        float rpm = 0.0f;
        RailMotion motion; // the newest state's
        bool horn = false;  // the horn flag as last shown
        bool shown = false; // shown since its last (re)start
        // The prediction's last basis (the newest state then) and the
        // drawing's correction: where it was drawn less where the newest
        // prediction puts it, blended away.
        std::optional<net::VehicleSnapshot> basis;
        RailMotion basisMotion;
        Vec3 offset;
        float offsetTurn = 0.0f;
        double updatedAt = 0.0;
    };
    void restart(Entry& entry, const net::AmbientEntity& e, std::uint32_t time);
    // The car predicted from `state` (its newest) to `time`; false when
    // `time` is not beyond it.
    bool predict(const Entry& entry, const net::VehicleSnapshot& state, const RailMotion& motion, double time,
                 PredictedPose& out) const;

    Options m_options;
    std::size_t m_catalogSize = 0;
    std::uint16_t m_catalog = 0;
    bool m_mismatch = false;
    bool m_any = false;
    std::uint32_t m_latest = 0; // the newest message's time
    std::uint32_t m_lightTime = 0, m_lightSteps = 0;
    std::map<int, Entry> m_entries;
    std::vector<Car> m_cars;
    Stats m_stats;
};

// The catalog both machines build: the ambient traffic's vehicle types in
// the AI map's order (ai::Traffic::types()), then the police posts' cars.
TrafficCatalog buildTrafficCatalog(std::span<const ai::VehicleData> types,
                                   std::span<const std::string> police);

// Host: an ambient car as shared. `model` and `paint` are its catalog index
// and paint job; `body` is its physics body's pose and motion while it has
// one (game::TrafficBodies), else null; `horn`: it honked since the last
// message.
struct TrafficBodyState {
    Mat34 transform;
    Vec3 velocity;
    Vec3 angularVelocity;
    // While the body is simulated (aiVehicleActive): WHL0-3's drawing
    // offsets (trafficWheelOffsets).
    std::optional<std::array<Vec3, 4>> wheels;
};

// Host: a knocked car's wheels as aiVehicleInstance::Draw draws them while it
// has a body (TrafficBodies::wheelsOf, world matrices), as offsets of WHL0-3
// from their pivots (`data`'s) in the model space of `transform`, the matrix
// the car is shared with.
std::array<Vec3, 4> trafficWheelOffsets(const Mat34& transform, const std::array<Mat34, 6>& wheels,
                                        const ai::VehicleData& data);
// Client: the wheels' world matrices from the offsets, for AiRenderer's
// physical cars: WHL0-3 unturned at their pivots plus the offsets, WHL4 and
// WHL5 at their pivots raised by WHL2's and WHL3's drawn height less the
// wheel radius (as TrafficBodies::wheelsOf places them).
void trafficWheelMatrices(const Mat34& transform, const std::array<Vec3, 4>& offsets,
                          const ai::VehicleData& data, std::array<Mat34, 6>& matrices,
                          std::array<bool, 6>& valid);
SharedCar shareTrafficCar(const ai::AmbientCar& car, int model, int paint, const TrafficBodyState* body,
                          bool horn);

// Client: a received traffic car as the renderer, the audio and the
// physics take an ambient car. `data` is the client's vehicle data of its
// model (null when it has none), `paintJobs` the model's paint jobs and
// `tireRotation` the wheels' turn the client keeps for it.
ai::AmbientCar ambientCarOf(const TrafficClient::Car& car, const std::string& model,
                            const ai::VehicleData* data, int paintJobs, float tireRotation);

} // namespace mm2::game
