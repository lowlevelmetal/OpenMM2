#pragma once

// The props of a network race, simulated by the host for everyone (OpenMM2;
// docs/multiplayer.md, "Props", and net/PropState.h).
//
// MM2 sends nothing about props (mmGameMulti, mmNetObject): every machine
// knocks its own props with its own simulation of every car, so the props
// knocked, where they rest and the parts thrown off the cars differ from
// screen to screen. In OpenMM2 the host is the authority:
//
//   PropHost    host side: the placed props that broke loose (reliable
//               events, and every one so far to a machine that has just
//               loaded the race) and the ring of knocked-over props, sent to
//               every client about 20 times a second while any moves
//   PropClient  client side: the host's knocks applied when the props are
//               shown at their time, the host's ring shown in mirror slots
//               (interpolated like the other players' cars), and this
//               machine's own predictions: a prop its car knocks moves at
//               once and hands over to the host's when it comes to rest, and a
//               knock the host never confirms is undone
//
// Nothing here touches the network: the race screen sends and feeds in the
// messages.

#include "core/Math.h"
#include "game/bangers/BangerSet.h"
#include "net/PropState.h"
#include "net/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace mm2::game {

// A checksum of a set's placed props (index, model, paint job and placement
// to the centimetre): every machine places the same props in the same order,
// so a prop's index names it on every machine. The host sends its checksum
// with every message; a client whose own differs cannot follow the host's
// props.
std::uint32_t propCatalog(const bangers::BangerSet& set);
// The number of placed props (the instances before the first hit instance).
std::size_t placedProps(const bangers::BangerSet& set);

// The BangerSet tag (Instance::tag) of a car part thrown off a car: whose car
// (a player's, by id; a shared traffic or police car, by catalog index), the
// part (damagePartIndex) and the car's paint job. Never 0.
std::uint32_t carPartTag(net::PropOwner owner, int ownerId, int part, int paint);
std::optional<net::PropDescriptor> carPartOfTag(std::uint32_t tag);
// What a hit instance of the set is across machines; nullopt for one that
// has no name there (a single-player opponent's part).
std::optional<net::PropDescriptor> describeHit(const bangers::BangerSet::Instance& inst);

class PropHost {
public:
    struct Options {
        // A message at most this often, while any prop moves fast or a slot
        // changed; every slowIntervalMs while props only creep (slower than
        // slowSpeed and slowSpin: a prop sliding down a hill, which MM2's
        // phSleep may never put to sleep); else every idleIntervalMs.
        std::uint32_t intervalMs = 50;
        std::uint32_t slowIntervalMs = 200;
        std::uint32_t idleIntervalMs = 500;
        float slowSpeed = 0.5f; // m/s
        float slowSpin = 1.0f;  // rad/s
        // A slot at rest carries its state in the first messages after it
        // changed, then in every tenth one (by slot number).
        int freshMessages = 3;
        int restRotation = 10;
    };

    PropHost() : PropHost(Options{}) {}
    explicit PropHost(const Options& options) : m_options(options) {}

    // Once a frame after the props' update (BangerSet::update): the placed
    // props that broke loose this frame (BangerSet::takeKnocks, which the
    // caller passes on) at session time `time`.
    void knocked(std::span<const bangers::BangerSet::Knock> knocks, std::uint32_t time);
    // The knocks to send to everyone since the last call (PropKnocksEvent
    // payloads, each within the protocol's 1 KiB).
    std::vector<std::vector<std::byte>> takeKnockEvents();
    // Every placed prop not standing, for a machine that has just loaded the
    // race (catch-up events).
    static std::vector<std::vector<std::byte>> catchUp(const bangers::BangerSet& set, std::uint32_t time);

    // The ring at session time `time` (the state after this frame's
    // simulation steps), when a message is due at monotonic time `nowMs`.
    // The same message goes to every client.
    std::optional<net::PropStateMsg> build(const bangers::BangerSet& set, std::uint32_t time,
                                           std::uint64_t nowMs, std::uint32_t catalog);

    struct Stats {
        std::uint64_t messages = 0, slots = 0, fullStates = 0, bytes = 0, knocks = 0;
    };
    Stats& stats() { return m_stats; }

private:
    struct SlotSeen {
        std::uint32_t generation = 0;
        bool moving = false;
        bool occupied = false;
        int fresh = 0;
    };
    Options m_options;
    std::vector<net::PropKnock> m_pending;
    std::uint32_t m_pendingTime = 0;
    std::vector<SlotSeen> m_seen;
    std::uint64_t m_lastSent = 0;
    bool m_sentAny = false;
    std::uint32_t m_sequence = 0;
    Stats m_stats;
};

class PropClient {
public:
    struct Options {
        // How far in the past the host's props are shown: what the messages
        // needed to arrive in time over the last 3 s, plus a send interval,
        // within these bounds (like the other players' cars, net::Session).
        double minDelayMs = 50.0;
        double maxDelayMs = 500.0;
        double initialDelayMs = 150.0;
        // A knock this machine predicted is undone when the host has not
        // made it by then.
        double predictTimeoutMs = 2000.0;
        // A piece simulated here hands over to the host's when it rests, or
        // after this long; the hand-over blends over blendMs.
        double handoverMs = 4000.0;
        double blendMs = 400.0;
        // Drawn this much before the state the physics sees (the scene is
        // drawn a simulation step behind, game::StepHistory).
        double drawBehindMs = 1000.0 / 60.0;
        // A piece simulated here that the host's ring does not hold (it threw
        // it differently, or reused its slot) disappears once at rest this
        // long after it was made.
        double orphanMs = 2000.0;
    };

    // `catalog`: this machine's propCatalog.
    explicit PropClient(std::uint32_t catalog) : PropClient(catalog, Options{}) {}
    PropClient(std::uint32_t catalog, const Options& options);

    // A message from the host (untrusted: checked as it is read) that arrived
    // at session time `arrival`.
    void receive(const net::PropStateMsg& msg, double arrival);
    // A knocks event from the host.
    void receiveKnocks(const net::PropKnocksEvent& event);
    // The placed props this machine's own car broke loose
    // (BangerSet::takeKnocks after its physics steps), at session time
    // `now`: predictions the host confirms or corrects.
    void predicted(std::span<const bangers::BangerSet::Knock> knocks, double now);
    // What update() did to the placed props since the last call: the host's
    // knocks applied (false) and this machine's predictions undone (true).
    std::vector<std::pair<std::size_t, bool>> takeApplied() { return std::exchange(m_applied, {}); }

    // Resolves a car part's descriptor to what the mirror draws (its banger
    // data, the car's model and the part's mesh); nullopt when this machine
    // cannot (the part is then not shown).
    using CarPartResolver =
        std::function<std::optional<bangers::BangerSet::MirrorSpec>(const net::PropDescriptor&)>;
    // Once a frame before the physics steps: the props as the host had them
    // at session time `now` less the delay. `now` is the session time the
    // simulation will have reached after this frame's steps.
    void update(bangers::BangerSet& set, double now, const CarPartResolver& resolve);

    double delayMs() const { return m_delay; }
    bool catalogMismatch() const { return m_mismatch; }
    // A placed prop the host has broken loose (known so far).
    bool hostBroken(std::size_t prop) const { return m_hostBroken.contains(prop); }

    struct Stats {
        std::uint64_t messages = 0, outdated = 0, refused = 0;
        std::uint64_t knocks = 0;       // the host's knocks applied
        std::uint64_t predicted = 0;    // knocks this machine's car made first
        std::uint64_t confirmed = 0;    // ... that the host made too
        std::uint64_t undone = 0;       // ... that it did not (the prop stood again)
        std::uint64_t handovers = 0;    // pieces simulated here handed to the host's
        std::uint64_t orphans = 0;      // pieces simulated here that the host's ring did not hold
    };
    const Stats& stats() const { return m_stats; }

    // Every hit instance shown (diagnostics): descriptor and drawn frame.
    struct Shown {
        net::PropDescriptor what;
        Mat34 matrix;
        bool moving = false;
        bool local = false; // simulated here
    };
    std::vector<Shown> shown(const bangers::BangerSet& set) const;

private:
    struct Generation {
        std::uint8_t generation = 0;
        net::PropDescriptor what;
        bool described = false;
        net::SnapshotBuffer buffer{16};
        std::uint32_t firstTime = 0;
        std::optional<std::uint32_t> goneAt;
    };
    struct Slot {
        std::deque<Generation> generations; // oldest first
        // Presentation.
        bool local = false;                 // the mirror was pushed by this machine's car
        double localSince = 0.0;            // ... at that time
        double holdUntil = 0.0;             // ... and is left where it stopped until then
        bool hostMoved = false;             // ... or until the host's has moved and rests again
        std::optional<Mat34> blendFrom;     // hand-over from what was shown here
        double blendStart = 0.0;
        std::optional<std::size_t> standIn; // the piece simulated here shown instead
        double standInSince = 0.0;
    };
    struct Prediction {
        double at = 0.0;
        bool confirmed = false;
    };
    struct LocalPiece {
        std::uint32_t generation = 0;
        double since = 0.0;
        std::optional<double> restedAt;
    };

    void applyKnock(bangers::BangerSet& set, std::size_t prop);
    static const Generation* shownAt(const Slot& slot, double time);
    static Generation* shownAt(Slot& slot, double time) {
        return const_cast<Generation*>(shownAt(static_cast<const Slot&>(slot), time));
    }
    std::optional<bangers::BangerSet::MirrorSpec> resolve(const bangers::BangerSet& set,
                                                          const net::PropDescriptor& what,
                                                          const CarPartResolver& carParts) const;
    std::optional<std::size_t> findStandIn(const bangers::BangerSet& set,
                                           const net::PropDescriptor& what) const;
    void updateDelay(double now);

    Options m_options;
    std::uint32_t m_catalog = 0;
    bool m_mismatch = false;
    bool m_any = false;
    std::uint32_t m_latest = 0;
    double m_delay = 0.0;
    double m_lastUpdate = -1.0;
    std::deque<std::pair<double, double>> m_lateness; // (arrival, lateness)
    std::map<int, Slot> m_slots;
    struct PendingKnock {
        std::size_t prop = 0;
        std::uint32_t time = 0;
        bool now = false; // a catch-up: at once
    };
    std::deque<PendingKnock> m_knocks; // the host's, waiting for their time
    std::set<std::size_t> m_hostBroken;
    std::map<std::size_t, Prediction> m_predicted;
    std::map<std::size_t, LocalPiece> m_pieces; // this machine's ring slots, by slot
    std::vector<std::pair<std::size_t, bool>> m_applied;
    Stats m_stats;
};

} // namespace mm2::game
