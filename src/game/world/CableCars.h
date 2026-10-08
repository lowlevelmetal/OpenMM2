#pragma once

// San Francisco's cable cars, after MM2's aiCableCar, aiCableCarInstance and
// the cable-car part of aiMap::Init / Update / Reset (midtown2.exe build
// 3393, MM2Recomp; documentation only).
//
// aiMap::Init creates one cable car (model va_cablecar_f) at every
// intersection where exactly one road with a cable-car line leaves
// (aiIntersection::IsCableCarStart): the ends of the lines. Only with the
// state pack's EnableCableCars, which mmGameMulti::Init clears: never in a
// network game. A car rides its road's cable-car line (the .bai side's tram
// polyline) section by section on a Hermite curve (aiRailSet), turns through
// intersections onto the next road with a line (straight across a crossing
// of two lines, back the same road at the end of the line), at most 15 m/s,
// accelerating at 1.5 to 3.5 m/s^2. It stops for the player, for other cable
// cars and for the ambient cars on its road ahead (CheckForObstacles), and
// at intersections as their stop signs and lights say
// (OkayToEnterIntersection). Its height and tilt come from three probes of
// the ground at its corners, except on flat roads. It rings and rumbles
// (aiCableCarAudio).
//
// The car is an unhit banger (aiCableCarInstance: lvlInstance flags 0x13)
// listed in its room, colliding as its banger data says; its ImpulseLimit2
// (7.6e9) keeps it standing like a wall.
//
// Where OpenMM2 differs (see docs/parity/mm2/world-objects.md): the
// ambient traffic does not see the cable cars (MM2 lists them in the same
// obstacle map, aiCableCar::UpdateObstacleMap), the four-way stops queue
// cable cars among themselves only, and a car that would break loose stays.

#include "ai/Driving.h"
#include "ai/World.h"
#include "game/Camera.h"
#include "game/CityLevel.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/fx/Random.h"
#include "phys/Level.h"
#include "phys/World.h"
#include "render/Device.h"

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace mm2::audio {
class Mixer;
class SoundBank;
} // namespace mm2::audio
namespace mm2::audio::game {
class CableCarAudio;
class Object3DManager;
} // namespace mm2::audio::game

namespace mm2::game::world {

class CableCars final : public InstanceSource {
public:
    // aiMap::Init's model, aiCableCar's top speed, reaction distance and
    // its stops: 2.5 m short of an obstacle, 0.25 m short of an
    // intersection; aiCableCar::CheckForObstacles' reach (30 m).
    static constexpr const char* kModel = "va_cablecar_f";
    static constexpr float kMaxSpeed = 15.0f;
    static constexpr float kReactDistance = 25.0f;
    static constexpr float kObstacleGap = 2.5f;
    static constexpr float kStopGap = 0.25f;
    static constexpr float kObstacleReach = 30.0f;

    // One cable car (aiCableCar).
    struct Car;
    // The car as the collision manager sees it (aiCableCarInstance).
    class Body;

    CableCars(ai::World& ai, const bangers::BangerDataLibrary& data, const bangers::BangerSet& bounds);
    ~CableCars() override;
    CableCars(const CableCars&) = delete;
    CableCars& operator=(const CableCars&) = delete;

    // aiMap::Init "Create the cable cars": a car at every cable-car start,
    // in intersection order, then each one's sister (DetermineSister).
    // `random` gives aiCableCar::Init's frand.
    void create(fx::Rand& random);
    // aiMap::Reset: every car back to its start (aiCableCar::Reset); the
    // world probes the ground and finds the rooms.
    void reset(const phys::World& world);
    // aiMap::Update's cable-car loop (after the racers): aiCableCar::Update.
    // `player` is aiMap's player (the obstacles CheckForObstacles reads
    // first); null for none.
    void update(float dt, const ai::TrackedCar* player, const phys::World& world);

    // aiCableCarAudio for every car (aiCableCar::Init).
    void loadAudio(audio::SoundBank& bank, audio::Mixer& mixer, audio::game::Object3DManager* manager);
    // aiCableCar::Update's audio part: each car's position and speed.
    void updateAudio(const Mat34& listener, float dt);
    void stopAudio();

    // aiCableCarInstance::Draw, as the rooms draw instances.
    void draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, const Frustum& frustum,
              const Camera& camera, const ObjectDetail& detail) const;

    // InstanceSource: the cable cars in `room`.
    void instancesIn(int room, std::vector<phys::Instance*>& out) const override;

    // Car i: where it is (the AI matrix, at the model's origin), how fast it
    // goes, the road and direction it rides, its room, its sister.
    std::size_t size() const { return m_cars.size(); }
    const Mat34& matrix(std::size_t i) const;
    float speed(std::size_t i) const;
    int path(std::size_t i) const;
    int dir(std::size_t i) const;
    int room(std::size_t i) const;
    int sister(std::size_t i) const;
    int startPath(std::size_t i) const;

    // aiIntersection::IsCableCarStart: exactly one road of `intersection`
    // leaves it on a cable-car line; its path index and direction.
    bool isCableCarStart(int intersection, int& path, int& dir) const;
    // aiCableCar::DetermineNextLink: the road with a line after `path`
    // (driven in `dir`) at the intersection it arrives at, and its
    // direction; false when there is none.
    bool nextLink(int path, int dir, int& next, int& nextDir) const;

private:
    const city::AiPath& source(int path) const;
    const std::vector<Vec3>* line(int path, int dir) const; // the side's cable-car line
    int sections(int path) const;
    float centerLength(int path, int a, int b) const;

    void resetCar(Car& c, const phys::World& world);
    void updateCar(Car& c, float dt, const ai::TrackedCar* player, const phys::World& world);
    void determineSister(Car& c);
    void solveVelocity(Car& c, float dt, const ai::TrackedCar* player);
    bool checkForObstacles(Car& c, float& distance, const ai::TrackedCar* player) const;
    bool okayToEnterIntersection(Car& c, float distance);
    float distanceToIntersection(const Car& c) const;
    void solveRailType(Car& c);
    void solvePositionAndOrientation(Car& c, const phys::World& world);
    void updateObstacleMap(Car& c);
    void moveToRoom(Car& c, int room);
    void updateRoom(Car& c, const phys::World& world);
    // aiIntersection's four-way stop queue (AddToStopSignCntl,
    // StopSignOkayToGo, RemoveFromStopSignCntl), for the cable cars.
    bool stopSignOkayToGo(int node, int car);
    void removeFromStopSign(int node, int car);
    // The obstacle map's per-section list of a road side (aiPath +0x90 /
    // +0xf4), the cable cars' entries.
    std::vector<int>* sectionList(int path, int dir, int section);
    const std::vector<int>* sectionList(int path, int dir, int section) const;

    ai::World& m_ai;
    const bangers::BangerDataLibrary& m_data;
    const bangers::BangerSet& m_bounds;
    std::vector<std::unique_ptr<Car>> m_cars;
    std::vector<std::vector<Body*>> m_rooms;
    std::map<std::array<int, 3>, std::vector<int>> m_sections; // (path, dir, section) -> cars
    std::map<int, std::vector<int>> m_nodeVehicles;            // intersection -> cars
    std::map<int, std::vector<int>> m_stopWaiting, m_stopAllowed;
};

} // namespace mm2::game::world
