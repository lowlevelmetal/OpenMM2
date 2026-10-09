#pragma once

// The damage of the cars drawn from the network: the other players' cars
// and, on a shared-traffic client, the host's police (OpenMM2; see
// docs/multiplayer.md, "Damage", and net/VehicleDamage.h).
//
// MM2 (mmNetObject::SetPositionData / PositionUpdate) sends a car's
// vehCarDamage::CurrentDamage with its position, and every machine simulates
// the network car, so its own collisions paint the dents and break the parts
// there. OpenMM2's network cars are kinematic, drawn from the owner's
// snapshots: their damage level still comes with the snapshot (the smoke,
// the fire and the tyre wobble follow it as vehCarDamage::Update makes
// them), and what the owner's vehCarDamage::ApplyImpact painted and broke
// comes as reliable events, replayed here through the same code:
//
//   DamageRecorder  owner side: the texel damage patches the car painted
//                   (the point and the random state each started from),
//                   the parts it lost, its damaging impacts (sparks,
//                   shards, impact sound) and its resets, sent in batches
//   DamageReplica   receiver side: one record per car since its last
//                   reset, each entry applied when the car is drawn at the
//                   time it happened on its owner's machine
//   applyDamage     the record's actions on a car's renderer and effects
//
// Nothing here touches the network: the race screen sends the recorder's
// events and feeds the received ones in.

#include "game/VehicleRenderer.h"
#include "game/fx/VehicleEffects.h"
#include "net/VehicleDamage.h"
#include "phys/vehicle/CarSim.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::game {

// The parts a car can lose, as bits of net::VehicleDamageEvent::parts:
// vehBreakableMgr::Impact's BREAK0-3, BREAK01, BREAK12, BREAK23, BREAK03 and
// the paint job's VARIANT (0-8), vehCarModel::EjectOneshot's WHL0-3, HUB0-3,
// FNDR0-1 and ENGINE (9-19). -1 for any other name.
int damagePartIndex(std::string_view part);
// The part's mesh name ("VARIANT<paintjob>" for 8); empty out of range.
std::string damagePartName(int index, int paintjob);
// EjectOneshot's parts fly at 1.3 times the car's speed, Impact's at 4 m/s.
bool damagePartIsWreckPart(int index);

// What a damaging impact (CarSim::onImpactCallback) shows on another
// machine, in the car's model space.
net::DamageImpact damageImpactOf(const phys::CarImpact& impact, const phys::CarSim& car);

// Damage level <-> the replicated 0..1 fraction (vehCarDamage::Update's
// (CurrentDamage - MedDamage) / (MaxDamage - MedDamage)): a network car's
// CurrentDamage is set from it, which gives the same smoke level, fire and
// tyre wobble (nothing shows below MedDamage).
float damageFromFraction(const phys::CarDamageParams& params, float fraction);

class DamageRecorder {
public:
    struct Options {
        // At most this often a batch of what happened; a reset sends the
        // batch before it at once.
        std::uint32_t intervalMs = 100;
        // The sender's own budget (under the host's 15 a second, burst 30):
        // what does not fit waits for the next tokens, nothing is dropped.
        double perSecond = 10.0;
        double burst = 20.0;
    };

    explicit DamageRecorder(std::uint16_t subject = net::kDamageOwnCar)
        : DamageRecorder(subject, Options{}) {}
    DamageRecorder(std::uint16_t subject, const Options& options);

    // vehCar::ClearDamage on the car (a reset, a respawn, a repair): its
    // damage starts again, everywhere, at session time `time`.
    void reset(std::uint32_t time);
    // fxTexelDamage::ApplyDamage is about to paint the car at `point` (model
    // space) from random state `seed`. Returns the point to paint at: the one
    // the receivers get (net::quantizeDamagePoint), so every machine runs
    // the texel damage on the same numbers.
    Vec3 patch(std::uint32_t time, const Vec3& point, std::uint32_t seed);
    // A part broke off (vehBreakableMgr::Eject), by mesh name.
    void part(std::uint32_t time, std::string_view name);
    // A damaging impact (for its sparks, shards and sound; the first few of
    // each batch travel).
    void impact(std::uint32_t time, const net::DamageImpact& impact);

    // The events to send at monotonic time `nowMs` (often empty).
    std::vector<net::VehicleDamageEvent> take(std::uint64_t nowMs);

    std::uint8_t epoch() const { return m_epoch; }
    // Patches recorded since the last reset (the ones beyond
    // net::kMaxDamageRecord are painted but not sent).
    std::uint32_t recorded() const { return m_count; }

private:
    struct Entry {
        std::uint32_t time = 0;
        net::DamagePatch patch;
    };
    struct Hit {
        std::uint32_t time = 0;
        net::DamageImpact impact;
    };
    // What happened in one epoch since the last event.
    struct Batch {
        std::uint8_t epoch = 0;
        std::optional<std::uint32_t> resetTime; // a new epoch's first batch: when it began
        std::uint32_t first = 0;                // record index of patches[0]
        std::vector<Entry> patches;
        std::uint32_t parts = 0; // every part since the reset
        std::optional<std::uint32_t> partsTime;
        std::vector<Hit> impacts;
        bool empty() const { return !resetTime && patches.empty() && !partsTime && impacts.empty(); }
    };
    Batch& open() { return m_batches.back(); }

    std::uint16_t m_subject;
    Options m_options;
    std::uint8_t m_epoch = 0;
    std::uint32_t m_count = 0; // patches this epoch
    std::uint32_t m_parts = 0;
    std::deque<Batch> m_batches; // the last one is open
    std::uint64_t m_lastSent = 0;
    bool m_sentAny = false;
    double m_tokens = -1.0;
    std::uint64_t m_tokensAt = 0;
};

class DamageReplica {
public:
    // A car's key: a player's own car, or a shared police car (ambient id).
    static std::uint32_t playerKey(std::uint8_t player) { return 0x10000u | player; }
    static std::uint32_t ambientKey(std::uint16_t id) { return id; }

    // An event from player `from` (untrusted: checked here; the payload was
    // decoded within the protocol's bounds), received at session time
    // `arrival`. A police car's damage is taken from the host only. Returns
    // false when refused.
    bool receive(std::uint8_t from, const net::VehicleDamageEvent& event, double arrival);

    struct Patch {
        Vec3 point;
        std::uint32_t seed = 0;
    };
    struct Actions {
        bool clear = false; // the car's damage was cleared: start from a clean car
        std::vector<Patch> patches;
        std::uint32_t partsOff = 0; // net::VehicleDamageEvent::parts bits
        bool partsFresh = false;    // thrown off as they break (else just gone)
        std::vector<net::DamageImpact> impacts;
        bool empty() const { return !clear && patches.empty() && partsOff == 0 && impacts.empty(); }
    };
    // What became due for a car drawn at session time `sampleTime` (the time
    // its owner's machine was at in what is drawn), at session time `now`.
    // An entry is due once the car is drawn at its time, or after
    // kMaxHoldMs whatever the time says; its sparks, shards, sound and
    // flying parts only while it is fresh (shown within kFreshMs of its
    // time).
    Actions advance(std::uint32_t key, double sampleTime, double now);
    // The records of cars not drawn this frame (not advanced since the last
    // call) take their entries after kMaxHoldMs, so a car that comes into
    // view later shows them (replay()).
    void settle(double now);
    // A car shown afresh (created, or reset by its drawing): clear, then
    // every patch and part of its record so far.
    Actions replay(std::uint32_t key) const;
    void forget(std::uint32_t key) { m_records.erase(key); }
    void clear() { m_records.clear(); }

    static constexpr double kMaxHoldMs = 2000.0;
    static constexpr double kFreshMs = 500.0;
    static constexpr std::size_t kMaxRecords = 96;
    // Events waiting for their time, per car: beyond it the oldest go into
    // the record at once (and the car replays its record when next drawn).
    static constexpr std::size_t kMaxPending = 64;

    struct Stats {
        std::uint64_t events = 0;
        std::uint64_t refused = 0;
        std::uint64_t patches = 0;
        std::uint64_t missed = 0; // patches a gap in the record indices skipped
    };
    const Stats& stats() const { return m_stats; }
    std::size_t records() const { return m_records.size(); }
    // The patches a car's record holds (tests, diagnostics).
    std::size_t recordSize(std::uint32_t key) const;

private:
    struct Pending {
        net::VehicleDamageEvent event;
        double arrival = 0.0;
        std::size_t nextPatch = 0, nextImpact = 0;
        bool started = false, partsDone = false, overdue = false;
    };
    struct Record {
        bool any = false;
        std::uint8_t epoch = 0;
        std::vector<Patch> patches; // applied, in record order
        std::uint32_t next = 0;     // record index after the newest applied patch
        std::uint32_t parts = 0;
        std::deque<Pending> pending;
        bool touched = false; // advanced since the last settle()
        bool replay = false;  // entries went in without actions: the next advance() replays all
    };
    void process(Record& record, double sampleTime, double now, Actions* out);

    std::map<std::uint32_t, Record> m_records;
    Stats m_stats;
};

// What applyDamage acts on: a network car as this frame draws it.
struct DamageTarget {
    VehicleRenderer* renderer = nullptr;
    fx::VehicleEffects* effects = nullptr; // sparks and shards (may be null)
    Mat34 body;                // the car's model matrix as drawn
    float speed = 0.0f;        // m/s (the parts' throw)
    float texelRadius = 0.4f;  // vehCarDamage's TextelDamageRadius
    // Takes a part off thrown as a banger at `speed` (vehBreakableMgr::Eject,
    // VehicleRenderer::detach with the banger); false when it cannot (no
    // banger data): applyDamage then just takes it off, as the owner's was.
    std::function<bool(const VehicleRenderer::Breakable& part, float speed)> eject;
    // AudImpact::Play for a replayed impact: world position, strength, id.
    std::function<void(const Vec3& position, float strength, int audioId)> sound;
};
void applyDamage(const DamageReplica::Actions& actions, DamageTarget& target);

class NetGame;
struct NetGameEvent;

// A network race's damage replication as the race screen runs it: this
// player's car's recorder, the host's police cars' recorders, the received
// records.
class NetDamage {
public:
    // This player's car.
    DamageRecorder& own() { return m_own; }
    // A shared police car on the host, by its ambient id (created on first use).
    DamageRecorder& police(std::uint16_t id);
    // Another player's car on the host, which simulates it (created on first
    // use; protocol 5).
    DamageRecorder& player(std::uint8_t id);
    void forgetPlayer(std::uint8_t id) { m_players.erase(id); }
    DamageReplica& replica() { return m_replica; }

    // Sends what the recorders have (reliable game events to everyone).
    void send(NetGame& net, std::uint64_t nowMs);
    // The frame's game events: the damage ones go into the records.
    void receive(std::span<const NetGameEvent> events, double now);
    // A drawn car's damage this frame: its record replayed first when it is
    // shown afresh, then what became due by `sampleTime`.
    void update(std::uint32_t key, DamageTarget& target, double sampleTime, double now, bool fresh);
    // After the drawn cars: the records of the others (DamageReplica::settle).
    void settle(double now) { m_replica.settle(now); }

    struct Stats {
        std::uint64_t sentEvents = 0, sentBytes = 0;
        std::uint64_t receivedEvents = 0, receivedBytes = 0;
    };
    const Stats& stats() const { return m_stats; }
    // Logs and clears the counts (every 10 s while anything was sent or
    // received; with `verbose` every event too).
    void logStats(std::uint64_t nowMs);
    void setVerbose(bool verbose) { m_verbose = verbose; }

private:
    DamageRecorder m_own;
    std::map<std::uint16_t, DamageRecorder> m_police;
    std::map<std::uint8_t, DamageRecorder> m_players;
    DamageReplica m_replica;
    Stats m_stats;
    std::uint64_t m_statsAt = 0;
    bool m_verbose = false;
};

} // namespace mm2::game
