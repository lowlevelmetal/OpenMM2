#pragma once

// A shared-traffic client's received cars as game::TrafficBodies' traffic
// (OpenMM2 extra; docs/multiplayer.md "Shared traffic", docs/review/
// multiplayer-desync-traffic.md).
//
// The received cars on their rails are instances of this machine's level as
// the host's rail cars are of its own (aiVehicleInstance), at their
// predicted places. When this machine's car hits one, the car takes a body
// at once (aiVehicleInstance::AttachEntity, aiVehicleActive) and the
// collision runs as it does on the host: the client's car pushes a car of
// its mass instead of meeting a wall that never gives way, and the car
// flies off when it is hit rather than a round trip later. Only this
// machine's car, and the cars it knocked loose, knock a car loose here; the
// host's police, the other players and the cars they knocked reach the
// client in its messages.
//
// The knocked car is this machine's until either the host's messages show it
// off its rail too and its body here has come to rest (or the host's car is
// 5 m from it): it then follows them, the drawing blending from where the
// local body left it; or they show it still on its rail at a time well after
// the hit, when the host would have knocked it (the host did not: it goes
// back to its rail, blended the same way). With the host simulating the
// client's car from the same inputs, the local body runs the host's physics
// from the same hit, so it is the better guess while it moves: the host's
// knocked car, a trip old and predicted along its velocity, is not.

#include "ai/Traffic.h"
#include "game/TrafficBodies.h"

#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace mm2::game {

class NetTrafficCars final : public TrafficBodies::Source {
public:
    // A received car: as an ambient car (game::ambientCarOf), and the session
    // time of its newest state from the host.
    struct Received {
        ai::AmbientCar car;
        std::uint32_t stateTime = 0;
    };
    // Each frame before TrafficBodies::beforeStep: the received traffic cars
    // at session time `now` (this machine's car's). A car this machine
    // knocked loose goes back to the host's once a state of it stamped later
    // than `confirmMs` after the hit still has it on its rail, or after
    // kMaxLocalMs whatever the host says.
    void update(std::span<const Received> received, double now, double confirmMs);
    static constexpr double kMaxLocalMs = 3000.0;
    // A car the host has knocked too stays this machine's until its body
    // comes to rest, unless the host's car is this far from it.
    static constexpr float kDivergeMetres = 5.0f;

    // A car this machine knocked loose and handed back to the host's
    // messages this frame, and where it was then.
    struct Handover {
        int id = 0;
        Mat34 pose;
        bool confirmed = false; // the host had knocked it too
    };
    std::vector<Handover> takeHandovers() { return std::exchange(m_handovers, {}); }
    // The cars this machine's car knocked loose since the last call.
    std::vector<int> takeKnocks() { return std::exchange(m_new, {}); }
    bool knocked(int id) const { return m_knocks.contains(id); }

    struct Stats {
        std::uint64_t knocks = 0;    // knocked loose by this machine's car
        std::uint64_t confirmed = 0; // the host knocked them too
        std::uint64_t withdrawn = 0; // the host kept them on their rails
    };
    const Stats& stats() const { return m_stats; }

    // TrafficBodies::Source: the received cars on their rails, and the ones
    // this machine knocked loose (physical, where their bodies are).
    const std::vector<ai::AmbientCar>& cars() const override { return m_cars; }
    void impact(int carId) override;
    void detach(int carId, const Mat34& pose, bool upright) override;
    void setPhysicalTransform(int carId, const Mat34& transform) override;
    bool attachable(int carId, bool byPlayer) const override;
    // A car knocked loose here knocks the next one loose too, as on the host.
    bool bodiesHitAsPlayer() const override { return true; }

private:
    struct Knock {
        int generation = 0;
        double time = 0.0; // session ms of the hit
        Mat34 pose;
        bool confirmed = false; // the host has knocked it too
        bool resting = false;   // its body came to rest
    };
    void handOver(int id, const Knock& knock, bool confirmed);
    // The car listed where the local body has it.
    void keepLocal(const ai::AmbientCar& c, const Knock& k);

    std::map<int, Knock> m_knocks;
    std::vector<ai::AmbientCar> m_cars;
    std::vector<Handover> m_handovers;
    std::vector<int> m_new;
    double m_now = 0.0;
    Stats m_stats;
};

} // namespace mm2::game
