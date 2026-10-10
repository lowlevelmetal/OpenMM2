#pragma once

// The props of a network race, simulated by the host (OpenMM2; see
// docs/multiplayer.md, "Props"). MM2 sends nothing about props: every machine
// knocks its own with its own simulation of every car. OpenMM2's host is the
// authority for them, as for everything else in a race.
//
// What a prop can be on every machine:
//
//   a placed prop   standing where the city placed it, or broken loose. Every
//                   machine places the same props in the same order, so its
//                   index (`prop`) names it everywhere; a checksum of the
//                   placement travels with every message.
//   a hit instance  one of the host's ring of knocked-over props
//                   (dgBangerManager, 40 slots, which a network game lets
//                   grow in a pile-up): a placed prop broken loose, one of
//                   its BREAKnn pieces, or a car part thrown off
//                   (vehBreakableMgr::Eject). A slot's generation changes
//                   whenever the host hands it out again.
//
// Two messages carry them:
//
//   PropStateMsg     host -> each client its own, unreliable, about 20 a
//                    second while anything near it moves (less often
//                    otherwise): every occupied ring slot, complete and in
//                    ascending order, so a slot missing from a newer message
//                    is empty from that message's time. The states are the
//                    client's: a moving slot near its car carries its state
//                    every time, one far away less often, one at rest now and
//                    then (in between only its slot and generation travel),
//                    the nearest first within a datagram.
//   PropKnocksEvent  host -> everyone, a reliable game event: the placed props
//                    that broke loose and when. Every machine thus knows every
//                    knocked prop of the race, wherever it is; a machine that
//                    reports the race loaded gets every knock so far at once.

#include "net/Protocol.h"

#include <cstdint>
#include <vector>

namespace mm2::net {

inline constexpr std::uint32_t kMaxPropIds = 32768;    // placed props 0..32767
inline constexpr std::uint32_t kMaxPropSlots = 256;    // ring slots 0..255 (MM2's ring has 40)
inline constexpr std::uint32_t kPropGenerations = 16;  // a slot's generation, modulo
inline constexpr std::uint32_t kMaxPropParts = 16;     // a prop's BREAK01..BREAK16
inline constexpr std::uint32_t kMaxPropOwners = 64;    // a car part's owner id
inline constexpr std::uint32_t kMaxPropCarParts = 20;  // game::damagePartIndex's parts
inline constexpr std::uint32_t kMaxPropPaint = 15;
// The frame at a prop's centre of gravity: +-8 km at 4 mm.
inline constexpr float kPropPositionRange = 8192.0f;
inline constexpr int kPropPositionBits = 22;
inline constexpr float kPropVelocityRange = 128.0f; // m/s
inline constexpr int kPropVelocityBits = 13;
inline constexpr float kPropSpinRange = 64.0f; // rad/s
inline constexpr int kPropSpinBits = 12;

enum class PropSource : std::uint8_t {
    Prop,     // a placed prop, whole
    PropPart, // one of a placed prop's BREAKnn pieces
    CarPart,  // a part thrown off a car
    Last = CarPart,
};
// Whose car a thrown part came from: a player's (by player id), or a shared
// traffic or police car (by the session's model catalog index,
// game::TrafficCatalog).
enum class PropOwner : std::uint8_t { Player, Catalog, Last = Catalog };

struct PropDescriptor {
    PropSource source = PropSource::Prop;
    std::uint16_t prop = 0; // Prop, PropPart: the placed prop
    std::uint8_t part = 0;  // PropPart: BREAK<part + 1>; CarPart: game::damagePartIndex
    PropOwner owner = PropOwner::Player; // CarPart
    std::uint8_t ownerId = 0;            // CarPart
    std::uint8_t paint = 0;              // CarPart: the car's paint job
    bool operator==(const PropDescriptor&) const = default;
};

struct PropSlot {
    std::uint8_t slot = 0;
    std::uint8_t generation = 0; // modulo kPropGenerations
    // False: only the slot and generation travel (it still holds the same
    // prop, at rest where a previous message put it).
    bool hasState = true;
    PropDescriptor what;
    bool moving = false;    // simulated on the host (an active), else at rest
    Vec3 position;          // the frame at the CG
    Quat orientation;
    Vec3 velocity;          // moving only
    Vec3 angularVelocity;   // moving only
};

// Host -> one client.
struct PropStateMsg {
    static constexpr MsgType kType = MsgType::PropState;
    std::uint32_t time = 0;    // session time the states belong to
    std::uint32_t catalog = 0; // checksum of the host's placed props (game::propCatalog)
    std::vector<PropSlot> slots;
};

template <class S>
bool serializePropDescriptor(S& s, PropDescriptor& d) {
    s.enumeration(d.source, PropSource::Last);
    if (d.source == PropSource::CarPart) {
        std::int32_t ownerId = d.ownerId, part = d.part, paint = d.paint;
        s.enumeration(d.owner, PropOwner::Last);
        s.ranged(ownerId, 0, static_cast<std::int32_t>(kMaxPropOwners) - 1);
        s.ranged(part, 0, static_cast<std::int32_t>(kMaxPropCarParts) - 1);
        s.ranged(paint, 0, static_cast<std::int32_t>(kMaxPropPaint));
        d.ownerId = static_cast<std::uint8_t>(ownerId);
        d.part = static_cast<std::uint8_t>(part);
        d.paint = static_cast<std::uint8_t>(paint);
        if constexpr (S::kReading)
            d.prop = 0;
        return s.ok();
    }
    std::int32_t prop = d.prop;
    s.ranged(prop, 0, static_cast<std::int32_t>(kMaxPropIds) - 1);
    d.prop = static_cast<std::uint16_t>(prop);
    if (d.source == PropSource::PropPart) {
        std::int32_t part = d.part;
        s.ranged(part, 0, static_cast<std::int32_t>(kMaxPropParts) - 1);
        d.part = static_cast<std::uint8_t>(part);
    } else if constexpr (S::kReading) {
        d.part = 0;
    }
    if constexpr (S::kReading) {
        d.owner = PropOwner::Player;
        d.ownerId = d.paint = 0;
    }
    return s.ok();
}

// `previous`: the slot before it in the message (-1 for the first); the
// slots ascend, the next one in a single bit.
template <class S>
bool serializePropSlot(S& s, PropSlot& p, std::int32_t previous = -1) {
    std::int32_t slot = p.slot, generation = p.generation;
    bool next = previous >= 0 && slot == previous + 1;
    s.boolean(next);
    if (next) {
        slot = previous + 1;
        if (slot >= static_cast<std::int32_t>(kMaxPropSlots))
            return s.fail();
    } else {
        s.ranged(slot, 0, static_cast<std::int32_t>(kMaxPropSlots) - 1);
        if (slot <= previous)
            return s.fail(); // not ascending
    }
    s.ranged(generation, 0, static_cast<std::int32_t>(kPropGenerations) - 1);
    p.slot = static_cast<std::uint8_t>(slot);
    p.generation = static_cast<std::uint8_t>(generation);
    s.boolean(p.hasState);
    if (!p.hasState)
        return s.ok();
    if (!serializePropDescriptor(s, p.what))
        return s.fail();
    s.boolean(p.moving);
    s.vec3Quantized(p.position, kPropPositionRange, kPropPositionBits);
    s.quat(p.orientation);
    if (p.moving) {
        s.vec3Quantized(p.velocity, kPropVelocityRange, kPropVelocityBits);
        s.vec3Quantized(p.angularVelocity, kPropSpinRange, kPropSpinBits);
    } else if constexpr (S::kReading) {
        p.velocity = {};
        p.angularVelocity = {};
    }
    return s.ok();
}

template <class S>
bool serialize(S& s, PropStateMsg& m) {
    s.u32(m.time);
    s.u32(m.catalog);
    auto count = static_cast<std::uint32_t>(std::min<std::size_t>(m.slots.size(), kMaxPropSlots));
    s.varU32(count);
    if (count > kMaxPropSlots)
        return s.fail();
    if constexpr (S::kReading)
        m.slots.resize(count);
    std::int32_t previous = -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!serializePropSlot(s, m.slots[i], previous))
            return s.fail();
        previous = m.slots[i].slot;
    }
    return s.ok();
}

// Bits one slot takes on the wire (after `previous`, as serializePropSlot).
std::size_t propSlotBits(const PropSlot& p, std::int32_t previous = -1);

// Host -> everyone, a game event (reliable, ordered): placed props that broke
// loose on the host. Each knock's time is `time` plus its delay; a catch-up
// (every knock of the race so far, to a machine that has just reported the
// race loaded) applies at once.
inline constexpr std::uint16_t kPropKnocksEvent = static_cast<std::uint16_t>(GameEventType::Custom) + 48;
inline constexpr std::size_t kMaxPropKnocksPerEvent = 512;
struct PropKnock {
    std::uint16_t prop = 0;
    std::uint16_t delay = 0; // ms after the event's time
};
struct PropKnocksEvent {
    std::uint32_t time = 0;
    bool catchUp = false;
    std::vector<PropKnock> knocks;
};

template <class S>
bool serialize(S& s, PropKnocksEvent& e) {
    s.u32(e.time);
    s.boolean(e.catchUp);
    auto count = static_cast<std::uint32_t>(std::min(e.knocks.size(), kMaxPropKnocksPerEvent));
    s.varU32(count);
    if (count > kMaxPropKnocksPerEvent)
        return s.fail();
    if constexpr (S::kReading)
        e.knocks.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t prop = e.knocks[i].prop;
        s.ranged(prop, 0, static_cast<std::int32_t>(kMaxPropIds) - 1);
        e.knocks[i].prop = static_cast<std::uint16_t>(prop);
        if (e.catchUp) {
            if constexpr (S::kReading)
                e.knocks[i].delay = 0;
        } else {
            s.u16(e.knocks[i].delay);
        }
    }
    return s.ok();
}

} // namespace mm2::net
