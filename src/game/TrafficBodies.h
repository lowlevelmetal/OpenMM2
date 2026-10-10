#pragma once

// Ambient traffic in the physics world, after MM2's aiVehicleInstance,
// aiVehicleManager, aiVehicleActive and vehWheelCheap (build 3393,
// MM2Recomp; documentation only).
//
// A car on its rail is a static instance of the level (aiVehicleInstance):
// its box bound sits where the AI put it and the city lists it in its room.
// When something hits it, World::collideInstances asks it for a body
// (AttachEntity): the car takes one of 32 rigid bodies (aiVehicleActive),
// leaves its rail in the AI and joins the movers. The body falls at
// 19.6 m/s^2 on four simple wheels (vehWheelCheap) and, once phSleep puts it
// to sleep (or it falls below y = -100), is handed back to the AI, which
// drives it back onto its lane when it stands upright on the ground or keeps
// it as a wreck.

#include "ai/World.h"
#include "game/CityLevel.h"
#include "net/TrafficFull.h"
#include "phys/World.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mm2::game {

// What aiVehicleActive::Impact does with an impact on a traffic car that
// left its rail; the game routes it to audio and the breakable parts.
struct TrafficImpact {
    int carId = -1;
    // AudImpact::Play(strength, audioId), then
    // aiAmbientVehicleAudio::PlayImpactHorn(strength) and
    // PlayImpactReaction(strength) (all three skipped when the car has no
    // ambient vehicle audio): strength is |x| + |y| + |z| of the impulse the
    // car took; audioId is the car's own collider id, which Attach sets to 0.
    float strength = 0.0f;
    int audioId = 0;
    // vehBreakableMgr::Impact(impulse, localPosition, room) when `breaks`
    // (|impulse| above 100). Every traffic car has a vehBreakableMgr
    // (aiVehicleInstance ctor: its threshold is 2500 instead of the
    // default 10000, its other two settings 11 and 4), holding the model's BREAK0-3
    // parts if it has any; at or above the threshold it ejects the part
    // nearest localPosition.
    float impulse = 0.0f;
    Vec3 localPosition; // the contact in the car's model space
    int room = 0;
    bool breaks = false;
};

class TrafficBodies final : public InstanceSource {
public:
    using ImpactCallback = std::function<void(const TrafficImpact&)>;

    // The traffic the cars belong to, aiVehicleAmbient's side of the
    // hand-over: the AI's (ai::World), or on a network client of a
    // shared-traffic cruise the cars received from the host (OpenMM2 extra,
    // game::NetTrafficCars), which the client's car may knock loose ahead of
    // the host.
    class Source {
    public:
        virtual ~Source() = default;
        virtual const std::vector<ai::AmbientCar>& cars() const = 0;
        // aiVehicleAmbient::Impact(1): the car left its rail for a body.
        virtual void impact(int carId) = 0;
        // aiVehicleActive::Detach: back from its body at `pose`, upright or
        // not (aiVehicleAmbient::Impact(0) or a wreck).
        virtual void detach(int carId, const Mat34& pose, bool upright) = 0;
        // aiGoalCollision::Update: the car's matrix follows its body.
        virtual void setPhysicalTransform(int carId, const Mat34& transform) = 0;
        // Whether a collision may give the car a body; `byPlayer`: the
        // local player's car is in it. The AI's cars always may.
        virtual bool attachable(int carId, bool byPlayer) const {
            (void)carId;
            (void)byPlayer;
            return true;
        }
        // Whether the cars' bodies count as the local player's in what they
        // hit (a client's knocked car knocks the next one loose, as on the
        // host). The AI's do not (nothing of theirs asks).
        virtual bool bodiesHitAsPlayer() const { return false; }
    };

    // The world's level (phys::World::setLevel) gives the rail cars their
    // rooms; the level must list instancesIn() (CityLevel::addSource).
    TrafficBodies(ai::World& ai, phys::World& world);
    TrafficBodies(Source& source, phys::World& world);
    const Source& source() const { return m_source; }
    ~TrafficBodies() override;
    TrafficBodies(const TrafficBodies&) = delete;
    TrafficBodies& operator=(const TrafficBodies&) = delete;

    // Once per frame after the AI update, before the physics step:
    // aiVehicleManager::Update (bodies that fell asleep, fell out of the
    // world or left every room stop being simulated; bodies whose car the AI
    // recycled are let go) and the rail cars follow the AI into their rooms.
    void beforeStep();
    // aiVehicleManager::Reset (a child of aiMap: aiMap::Reset reaches it
    // through asNode::Reset): every attached active detached
    // (aiVehicleActive::Detach: the car back to the AI, a wreck if it is not
    // standing upright) and the list emptied. Call before ai::World::reset.
    void reset();
    // Once per frame after the physics step: aiVehicleActive::PostUpdate
    // (the AI's matrix follows the body; asleep or below y = -100: handed
    // back to the AI).
    void afterStep();

    // InstanceSource: the rail cars of `room` that have no body.
    void instancesIn(int room, std::vector<phys::Instance*>& out) const override;

    // World transform (model origin) of a car that has a body, or that lost
    // it and the AI has not caught up with yet (its list is refreshed by its
    // next update); null when the AI transform is current.
    const Mat34* transformOf(int carId) const;
    // The wheels aiVehicleInstance::Draw draws for a car that has a body
    // (aiVehicleActive's vehWheelCheaps instead of the rail's turning
    // wheels): WHL0-5 world matrices, valid where the car has the pivot.
    // Nullopt for a car without a body.
    struct Wheels {
        std::array<Mat34, 6> matrix{};
        std::array<bool, 6> valid{};
    };
    std::optional<Wheels> wheelsOf(int carId) const;
    // The velocity and spin of a car that has a body (its ICS's); false
    // without one (OpenMM2: the shared traffic of a network cruise sends
    // them).
    bool motionOf(int carId, Vec3& velocity, Vec3& spin) const;
    // OpenMM2 (the shared traffic of a network cruise): the state of a car's
    // body as the host sends it in full (nullopt without a body), and on a
    // client the car put to such a state, given a body first when it has
    // none (aiVehicleManager::Attach without an impact). False when the
    // source does not list the car.
    std::optional<net::TrafficBodyState> bodyState(int carId) const;
    bool setBodyState(int carId, const net::TrafficBodyState& state);
    // The body of a car that has one (null without).
    phys::Body* body(int carId);
    // OpenMM2: the car whose body `b` is (-1: none of them).
    int carOfBody(const phys::Instance* b) const;
    // Cars with a body (at most 32).
    std::size_t activeCount() const { return static_cast<std::size_t>(m_count); }

    // OpenMM2 (a network client running its car's samples again): the cars
    // on their rails (no body) within `radius` of `at` and where they are;
    // and cars on their rails put somewhere for a while (their rooms follow;
    // the AI's list is not touched, the next beforeStep puts them back).
    struct RailPose {
        int id = -1;
        Mat34 transform; // model origin
    };
    std::vector<RailPose> railPoses(const Vec3& at, float radius) const;
    void placeRailCars(std::span<const RailPose> poses);

    // ?WeatherFriction@@3MA for the wheels' grip (mmGame::InitWeather: 0.8
    // in rain, 0.75 in rain at night, else 1).
    void setWeatherFriction(float friction) { m_weatherFriction = friction; }
    // Called during the physics step for every impact on a traffic car that
    // has a body.
    void setImpactCallback(ImpactCallback callback) { m_onImpact = std::move(callback); }

private:
    class RailCar; // aiVehicleInstance
    class Active;  // aiVehicleActive
    struct Wheel;  // vehWheelCheap
    struct BoundEntry {
        std::unique_ptr<phys::BoundBox> box;
        float radius = 0.0f;
    };

    RailCar& railCar(int id);
    const RailCar* findRailCar(int id) const;
    const BoundEntry& boundFor(const ai::VehicleData& data);
    // The rail cars by room (instancesIn's list), after their rooms changed.
    void listRooms();
    // aiVehicleManager::Attach / Detach.
    Active& attach(RailCar& car);
    void release(Active& active);
    // aiVehicleActive::Detach: out of the world, back to the AI.
    void detach(Active& active);
    // The body stops being simulated without going back to the AI.
    void drop(Active& active);

    std::unique_ptr<Source> m_ownSource; // the AI's, for the ai::World constructor
    Source& m_source;
    phys::World& m_world;
    // One per AI car id, created when the car first appears (stable
    // addresses: the level holds pointers during the step).
    std::vector<std::unique_ptr<RailCar>> m_railCars;
    std::unordered_map<const ai::VehicleData*, BoundEntry> m_bounds;
    // The rail cars by room for instancesIn(), sorted by room then id.
    std::vector<std::pair<int, RailCar*>> m_roomList;
    // aiVehicleManager: the 32 actives; m_order[0, m_count) are attached.
    std::vector<std::unique_ptr<Active>> m_actives;
    std::array<Active*, ai::kMaxPhysicalCars> m_order{};
    int m_count = 0;
    float m_weatherFriction = 1.0f;
    ImpactCallback m_onImpact;
};

} // namespace mm2::game
