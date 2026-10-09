#pragma once

// A network race's props as the race screen runs them (OpenMM2; see
// PropSync.h and docs/multiplayer.md, "Props"): the host's PropHost or a
// client's PropClient over the race's BangerSet, the messages and events
// through NetGame, and an opt-in trace for measuring how the machines'
// props differ.

#include "game/bangers/BangerSet.h"
#include "game/net/PropSync.h"

#include <cstdint>
#include <cstdio>
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

// Development aid: OPENMM2_DEBUG_NETPROPS=<file> writes, one line each, the
// placed props ("P ..."), every placed prop that broke loose on this machine ("K <session ms> <prop>
// <model> <cause>"), every knock this machine predicted and undid ("X ..."),
// and every 250 ms of session time ("T <ms>") the props not standing
// ("B <ids>"), the knocked-over props and thrown parts shown ("H <what> <x>
// <y> <z> <moving> <local>"), the cars' damage ("D <car> <level> <dents>")
// and this machine's car's damaging impacts ("I <ms> <cause> <value>
// <damage>"). `netprobe propdiff` compares two machines' files.
class PropTrace {
public:
    static std::unique_ptr<PropTrace> fromEnvironment();
    explicit PropTrace(std::FILE* file) : m_file(file) {}
    ~PropTrace();
    PropTrace(const PropTrace&) = delete;
    PropTrace& operator=(const PropTrace&) = delete;

    // Every placed prop once ("P <prop> <model> <x> <y> <z>", its ground point).
    void placed(const bangers::BangerSet& set);
    void knock(double t, std::size_t prop, std::string_view model, std::string_view cause);
    void undo(double t, std::size_t prop);
    void impact(double t, std::string_view cause, float value, float damage);
    // Starts a tick when `t` passed the next 250 ms boundary.
    bool tick(double t);
    void broken(const bangers::BangerSet& set);
    void shown(const net::PropDescriptor& what, const Mat34& m, bool moving, bool local);
    // A car's damage level (0..1, as replicated) and its dents.
    void car(std::string_view name, float level, float dents);

private:
    std::FILE* m_file = nullptr;
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
    void setup(NetGame& net, bangers::BangerSet& set,
               std::function<bool(const phys::Instance&)> localToucher);
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

    // The trace, when OPENMM2_DEBUG_NETPROPS is set; `traced()` tells whether
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
    std::unique_ptr<PropTrace> m_trace;
    bool m_traced = false;
    std::uint64_t m_statsAt = 0;
    std::uint64_t m_sentBytes = 0, m_sentMessages = 0, m_sentEvents = 0, m_eventBytes = 0;
};

} // namespace mm2::game
