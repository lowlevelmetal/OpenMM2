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
#include "net/AmbientState.h"
#include "net/Snapshot.h"

#include <cstddef>
#include <cstdint>
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
    Vec3 velocity;
    Vec3 angularVelocity;
    std::uint8_t flags = 0; // net::AmbientFlags
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
        // As the remote players (net::SessionConfig): shown this far in the
        // past, extrapolated at most this far beyond the newest message.
        double interpolationDelayMs = 100.0;
        double maxExtrapolationMs = 250.0;
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
    // The cars at `renderTime` (session ms, normally session time less the
    // interpolation delay); cars that have left by then are dropped.
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
        std::uint8_t target = net::kAmbientNoTarget;
        float damage = 0.0f;
        float rpm = 0.0f;
        float throttle = 0.0f;
        int gear = 0;
        bool extrapolated = false; // beyond the newest message (late or lost packets)
        bool hornStarted = false;  // the horn sounded since the previous update
        bool fresh = false;        // shown for the first time (or put somewhere new) this update
    };
    const std::vector<Car>& cars() const { return m_cars; }
    // The host's traffic light steps at `renderTime`, once a message came.
    std::optional<std::uint32_t> lightSteps(double renderTime) const;
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
    };
    const Stats& stats() const { return m_stats; }
    const Options& options() const { return m_options; }

private:
    struct Entry {
        int generation = 0;
        net::AmbientKind kind = net::AmbientKind::Traffic;
        int model = 0;
        int paint = 0;
        net::SnapshotBuffer buffer{32};
        std::uint32_t firstTime = 0; // its first snapshot (hidden before it)
        std::uint32_t lastTime = 0;  // its newest snapshot
        std::optional<std::uint32_t> goneAt; // missing from a newer message: gone from this time on
        std::uint8_t target = net::kAmbientNoTarget;
        float rpm = 0.0f;
        bool horn = false;  // the horn flag as last shown
        bool shown = false; // shown since its last (re)start
    };
    void restart(Entry& entry, const net::AmbientEntity& e, std::uint32_t time);

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
};
SharedCar shareTrafficCar(const ai::AmbientCar& car, int model, int paint, const TrafficBodyState* body,
                          bool horn);

// Client: a received traffic car as the renderer, the audio and the
// physics take an ambient car. `data` is the client's vehicle data of its
// model (null when it has none), `paintJobs` the model's paint jobs and
// `tireRotation` the wheels' turn the client keeps for it.
ai::AmbientCar ambientCarOf(const TrafficClient::Car& car, const std::string& model,
                            const ai::VehicleData* data, int paintJobs, float tireRotation);

} // namespace mm2::game
