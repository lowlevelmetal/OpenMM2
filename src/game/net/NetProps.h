#pragma once

// A network race's props as the race screen runs them (OpenMM2; see
// PropSync.h and docs/multiplayer.md, "Props"): the host's PropHost or a
// client's PropClient over the race's BangerSet, the messages and events
// through NetGame, and an opt-in trace for measuring how the machines'
// props differ.

#include "game/bangers/BangerSet.h"
#include "game/net/PlayerCars.h"
#include "game/net/PropSync.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace mm2::game {

class NetGame;
struct NetGameEvent;

// Development aid: the props' lines of the OPENMM2_NET_TRACE file (lowercase
// tags; `netprobe syncreport` compares the machines' props from them): the
// placed props once ("p <prop> <model> <x> <y> <z>"), every placed prop that
// broke loose on this machine ("k <session ms> <prop> <model> <cause>"), every
// knock this machine predicted and undid ("x <ms> <prop>"), and every 250 ms
// of session time ("t <ms> <actual ms>") the props not standing ("b <ids>"),
// the knocked-over props and thrown parts shown ("h <what> <x> <y> <z>
// <moving> <local>"), the cars' damage ("d <car> <level> <dents>") and this
// machine's simulated cars' damaging impacts ("i <ms> <car> <cause> <value>
// <damage>").
class PropTrace {
public:
    // Null unless the session traces (NetGame::tracing).
    static std::unique_ptr<PropTrace> of(NetGame& net);
    explicit PropTrace(NetGame& net) : m_net(net) {}
    PropTrace(const PropTrace&) = delete;
    PropTrace& operator=(const PropTrace&) = delete;

    // Every placed prop once (its ground point).
    void placed(const bangers::BangerSet& set);
    void knock(double t, std::size_t prop, std::string_view model, std::string_view cause);
    void undo(double t, std::size_t prop);
    // A damaging impact on `car` in this machine's simulation (its own car,
    // and on the host the other players' cars), its value and the damage
    // after it.
    void impact(double t, std::string_view car, std::string_view cause, float value, float damage);
    // Starts a tick when `t` passed the next 250 ms boundary.
    bool tick(double t);
    void broken(const bangers::BangerSet& set);
    void shown(const net::PropDescriptor& what, const Mat34& m, bool moving, bool local);
    // A car's damage level (0..1, as replicated) and its dents.
    void car(std::string_view name, float level, float dents);

private:
    NetGame& m_net;
    double m_next = -1.0;
};

std::string describe(const net::PropDescriptor& what);

class NetProps {
public:
    NetProps();
    ~NetProps();
    NetProps(const NetProps&) = delete;
    NetProps& operator=(const NetProps&) = delete;

    // A network race, once its props are placed: the host simulates them
    // for everyone; a client follows it, its props touched only by
    // `localToucher` (its own car). OPENMM2_NETPROPS=local leaves every
    // machine with its own props, as MM2 and OpenMM2 0.3 did (for
    // comparison).
    // `carOf` (host): where a client's car is, for what its messages carry
    // first (none: anywhere). In a network race the ring of knocked-over
    // props grows in a pile-up, up to kMaxHit a player (BangerSet::
    // setRingGrowth).
    using CarOf = std::function<std::optional<Vec3>(std::uint8_t)>;
    void setup(NetGame& net, bangers::BangerSet& set,
               std::function<bool(const phys::Instance&)> localToucher, CarOf carOf = {});
    // Client: which player's car touched a prop (its predictions), where
    // the host had a player's car when, and where this machine's car is (its
    // missed predictions undone).
    using OwnCarAt = std::function<std::optional<Vec3>()>;
    void setClientCars(PropClient::CarOfToucher carOfToucher, PropClient::HostCar hostCar,
                       OwnCarAt ownCarAt) {
        m_carOfToucher = std::move(carOfToucher);
        m_hostCar = std::move(hostCar);
        m_ownCarAt = std::move(ownCarAt);
    }
    // Host: whether a player is still in the race (one who quit it has no
    // car and is sent nothing).
    void setInRace(std::function<bool(std::uint8_t)> inRace) { m_inRace = std::move(inRace); }
    bool host() const { return m_host.has_value(); }
    bool client() const { return m_client.has_value(); }
    // A client takes the cars' thrown parts from the host's ring (except
    // its own car's, which it throws at once and hands over).
    bool hostThrowsParts() const { return m_client && !m_client->catalogMismatch(); }

    // Client, before the frame's physics steps: the host's messages and
    // knocks applied, the host's props placed for session time `now` (the
    // time the simulation will have reached after the steps).
    void beforeStep(NetGame& net, std::span<const NetGameEvent> events, double now,
                    const PropClient::CarPartResolver& resolve);
    // Every machine, after the props' update (BangerSet::update): the
    // frame's knocks (the host sends them, a client takes them as its
    // predictions), the host's message, the trace. `classify` names what
    // broke a prop loose (for the trace).
    using Classifier = std::function<std::string(const phys::Instance*)>;
    void afterStep(NetGame& net, double now, std::uint64_t nowMs, const Classifier& classify);

    // Host, with the CarStates it sends client `id` this frame (session time
    // `time`): the pieces round that client's car in full (net::PropFull).
    void sendFull(NetGame& net, std::uint8_t id, std::uint32_t time, const phys::Body& car);
    // Client, in its car's acknowledgement of the CarStates at `time`: the
    // host's moving pieces round its car as companions of its car's samples
    // run again (put to the host's state there and simulated with it); their
    // bodies join `bodies` (the race screen's companions' bodies). `now`:
    // this frame's session time; `car`: this machine's car. `recorded`: where this machine had a body
    // after the acknowledged sample (its frame and velocity), if it knows: a
    // piece it had within the car's tolerances there (game::CarPrediction::
    // Options) needs no samples run again for it.
    using Recorded = std::function<std::optional<std::pair<Mat34, Vec3>>(const phys::Body*)>;
    void fullCompanions(std::uint32_t time, double now, std::vector<CarPrediction::Companion>& out,
                        std::vector<const phys::Body*>& bodies, phys::Body& car,
                        const Recorded& recorded = {});
    // ... and after it: those states used.
    void afterReplay(std::uint32_t time);

    // The trace, when OPENMM2_NET_TRACE is set; `traced()` tells whether
    // afterStep began a tick this frame (the race adds its cars' lines).
    PropTrace* trace() { return m_trace.get(); }
    bool traced() const { return m_traced; }

    const std::optional<PropClient>& clientState() const { return m_client; }

private:
    void sendCatchUps(NetGame& net, std::uint32_t time);
    void logStats(NetGame& net, std::uint64_t nowMs);

    bangers::BangerSet* m_set = nullptr;
    std::optional<PropHost> m_host;
    std::optional<PropClient> m_client;
    std::uint32_t m_catalog = 0;
    std::set<std::uint8_t> m_caughtUp; // host: players sent every knock so far
    CarOf m_carOf;
    std::function<bool(std::uint8_t)> m_inRace;
    PropClient::CarOfToucher m_carOfToucher;
    PropClient::HostCar m_hostCar;
    OwnCarAt m_ownCarAt;
    std::unique_ptr<PropTrace> m_trace;
    bool m_traced = false;
    std::uint64_t m_statsAt = 0;
    std::uint64_t m_sentBytes = 0, m_sentMessages = 0, m_sentEvents = 0, m_eventBytes = 0;
    std::uint64_t m_fullBytes = 0;
};

} // namespace mm2::game
