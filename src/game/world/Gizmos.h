#pragma once

// The city's scripted objects ("gizmos"), after MM2's mmGame::InitGizmos,
// init_gizmo_mgr and the gizmo managers (midtown2.exe build 3393,
// MM2Recomp; documentation only):
//
//   gizSailboatMgr / gizSailboat   boats and windsurfers (gizInstance) on
//                                  closed spline loops; drawn, not collided
//   gizBridgeMgr / gizBridge       drawbridges (one leaf per path, two for a
//                                  three-point path) that lift and lower on a
//                                  timer, on the player's approach, stay open
//                                  or never move; with their sounds
//   gizTrainMgr / gizTrain(Car)    London's tube trains: three cars running
//                                  a tunnel path, stopping and reversing at
//                                  both ends; with their sounds
//   gizFerryMgr / gizFerry         ferries and boats on spline loops (still
//                                  on a two-point path); with their sounds
//   gizParkedCarMgr                parked cars along the streets: ordinary
//                                  props (bangers), see placeParkedCars
//
// Each kind comes from a path set, race/<city>/<city>_<kind>.pathset, or the
// race's own <city>_<kind>_<race>.pathset when it has one (init_gizmo_mgr).
// Bridges, ferries and train cars are unhit bangers (dgUnhitBangerInstance)
// with their lvlInstance flags 0x132: collidable, terrain-collidable and hit
// by the wheels, but never knocked loose (flag 1 cleared). The physics world
// sees them through the level's room lists (InstanceSource); they move as
// static objects (MM2 gives them no velocity).

#include "game/Camera.h"
#include "city/CityData.h"
#include "game/CityLevel.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/RaceConfig.h"
#include "game/TextureLibrary.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/bangers/PropPlacement.h"
#include "game/fx/Random.h"
#include "game/world/PathSpline.h"
#include "phys/Level.h"
#include "render/Device.h"
#include "vfs/Vfs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm2::audio {
class Mixer;
class SoundBank;
} // namespace mm2::audio
namespace mm2::audio::game {
class AmbientObject;
class BridgeAudio;
class Object3DManager;
class SubwayAudio;
} // namespace mm2::audio::game

namespace mm2::game::world {

// init_gizmo_mgr's path set: "race/<city>/<city>_<kind>_<race>.pathset" in a
// race mode (not cruise) when the file exists, else
// "race/<city>/<city>_<kind>.pathset"; empty when neither exists (no
// manager). <race> is dgGameModeNames' name with the race index
// (racePropsName).
std::string gizmoPathSetPath(const vfs::Vfs& vfs, std::string_view city, std::string_view kind, GameMode mode,
                             int raceIndex);

// mmGame::InitGizmos: which managers a session creates. Sailboats, bridges
// and trains always; ferries only in single player; parked cars in single
// player and in the network races, but not in network cruise or Cops and
// Robbers.
struct GizmoKinds {
    bool sailboats = true, bridges = true, trains = true, ferries = true, parkedCars = true;
    static GizmoKinds forSession(GameMode mode, bool multiplayer);
};

// gizParkedCarMgr::Init and gizParkedCarMgr_EnumeratePath: every path is
// walked as dgPath::Enumerate walks it, at its spacing but at least 5 m.
// At each point irand() % 3 picks nothing (0) or giz_pcar01_l / giz_pcar02_l
// (whatever the city), turned a quarter turn about Y from the path's frame
// (Matrix34::Dot with MakeRotateY(pi/2)), full matrix, with the paint job
// irand() (dgBangerInstance::SetVariant takes it modulo the model's
// variants). The cars are ordinary props: the caller adds them to the
// BangerSet, which finds their rooms.
std::vector<bangers::PlacedProp> placeParkedCars(const city::PathSet& set, fx::Rand& random);

// A gizmo the physics world collides with: a dgUnhitBangerInstance whose
// owner moves it (dgUnhitYBangerInstance for the ferries, which keeps only
// a rotation about Y; dgUnhitMtxBangerInstance for bridges and train cars).
class GizmoBody final : public phys::Instance {
public:
    // dgBangerInstance::GetBound: the banger data's bound (for 1, the box
    // around a bound that is not a box).
    const phys::Bound* bound(int which) const override;
    // GetMatrix: the frame at the centre of gravity.
    const Mat34& matrix() const override { return m_matrix; }
    float radius() const override;

    // dgUnhitYBangerInstance::SetMatrix (position and the X row's x and z)
    // or dgUnhitMtxBangerInstance::SetMatrix (the whole matrix).
    void setMatrix(const Mat34& m);

    std::string model;
    const bangers::BangerData* data = nullptr;
    const bangers::BangerSet* bounds = nullptr;
    bool yOnly = false;
    int paint = 0;
    bool listed = false; // in the owner's room list

private:
    Mat34 m_matrix;
};

class Gizmos final : public InstanceSource {
public:
    // gizSailboat: Init's gizInstance with its spline.
    struct Sailboat {
        std::string model;
        const bangers::BangerData* data = nullptr;
        float cgY = 0.0f; // gizInstance +0x44: the banger data's CG height
        int paint = 0;    // gizInstance +0x48
        PathSpline spline;
        Mat34 matrix;     // gizInstance +0x14 (SetMatrix adds cgY to the height)
        int room = 0;
    };
    // gizBridge.
    struct Bridge {
        enum class Type { Inactive = 0, Proximity = 1, Timed = 2, Open = 3 };
        enum class State { Down = 0, Raising = 1, Up = 2, Lowering = 3 };
        std::unique_ptr<GizmoBody> body;
        Mat34 placement;          // +0x64: the hinge's frame from the path
        Type type = Type::Timed;  // +0x5c (the constructor's)
        State state = State::Down; // +0x58
        float timer = 0.0f;       // +0x60
        float angle = 0.0f;       // +0x94
        int partner = -1;         // +0x98: the other leaf of a double bridge
        std::unique_ptr<audio::game::BridgeAudio> audio;
    };
    // gizTrainCar.
    struct TrainCar {
        std::unique_ptr<GizmoBody> body;
        PathSpline spline;
    };
    // gizTrain.
    struct Train {
        enum class State { InStation = 0, Accelerating = 1, Running = 2, Braking = 3 };
        State state = State::InStation; // +0x00
        bool forward = true;            // +0x04
        float timer = 0.0f;             // +0x08
        float speedFactor = 1.0f;       // +0x0c
        std::array<TrainCar, 3> cars;   // +0x10
        std::unique_ptr<audio::game::SubwayAudio> audio; // +0x130
    };
    // gizFerry.
    struct Ferry {
        std::unique_ptr<GizmoBody> body;
        PathSpline spline;
        std::unique_ptr<audio::game::AmbientObject> audio; // +0x40, "ferry"
    };

    // gizBridge: lift speed (radians per second), open angle, and the
    // seconds a timed bridge stays down / up (its static tuning).
    static constexpr float kLiftSpeed = 0.05f;
    static constexpr float kGoalAngle = 0.471238881f;
    static constexpr float kDownInterval = 10.0f;
    static constexpr float kUpInterval = 10.0f;
    // gizBridgeMgr::CheckProximity: 100 m, squared.
    static constexpr float kProximityDist2 = 10000.0f;
    // gizBridgeMgr / gizFerryMgr: the distance of the first LOD step.
    static constexpr float kCullDistance = 200.0f;
    // gizTrain / gizTrainCar: full speed (m/s), the seconds in a station,
    // the speed factor's change per second, and the seconds of travel
    // between the cars at Reset.
    static constexpr float kTrainSpeed = 40.0f;
    static constexpr float kStationWait = 10.0f;
    static constexpr float kTrainAccel = 0.51f;
    static constexpr float kCarSpacing = 0.44f;
    // gizFerryMgr: the ferries' speed and its variation; gizSailboatMgr: the
    // sailboats' initial speed and their variation about the path's spacing.
    static constexpr float kFerrySpeed = 0.75f;
    static constexpr float kFerrySpeedVariation = 0.0f;
    static constexpr float kSailboatSpeed = 4.0f;
    static constexpr float kSailboatSpeedVariation = 1.0f;

    struct Options {
        std::string city; // MMSTATE's city name ("london", "sf")
        GameMode mode = GameMode::Cruise;
        int raceIndex = -1;
        bool multiplayer = false;
    };

    // `bounds` provides the banger data's collision bounds.
    Gizmos(const bangers::BangerDataLibrary& data, const bangers::BangerSet& bounds);
    ~Gizmos() override;
    Gizmos(const Gizmos&) = delete;
    Gizmos& operator=(const Gizmos&) = delete;

    // mmGame::InitGizmos without the parked cars (see placeParkedCars):
    // sailboats, bridges, trains, then (single player) ferries. `level`
    // finds rooms (lvlLevel::FindRoomId); gizBridge::Init sets the level
    // room flag 0x10 (LevelRoomFlag::Bridge) in `roomFlags` at each bridge
    // and 5 m above it. A network game in London opens every bridge.
    // `random` is the game's irand / frand for the choices made here.
    void load(const vfs::Vfs& vfs, const Options& options, const phys::Level* level,
              std::vector<std::uint16_t>* roomFlags, fx::Rand& random);
    // The objects' sounds: "drawbridge" (mmBridgeAudio), "subwaycar"
    // (aiSubwayAudio) and "ferry" (Aud3DAmbientObject).
    void loadAudio(const vfs::Vfs& vfs, audio::SoundBank& bank, audio::Mixer& mixer,
                   audio::game::Object3DManager* manager);

    // The managers' Reset (mmGame::Reset): bridges down (open ones open),
    // trains in their first station, ferries and sailboats back at their
    // paths' starts.
    void reset();
    // The managers' Update, once per unpaused frame before the physics step
    // (they are nodes of the game, which mmGameManager updates first).
    // `trigger` is the player's car (gizBridgeMgr's only proximity trigger:
    // mmGame::InitGizmos also adds the opponents, but aiMap has none yet).
    void update(float dt, const std::optional<Vec3>& trigger);
    // The sounds, after update(): each object's Aud3DAmbientObject::Update
    // with the listener.
    void updateAudio(const Mat34& listener, float dt, bool inTunnel);
    void stopAudio();

    // gizBridgeMgr::Cull and gizFerryMgr::Cull (their own distances), the
    // train cars and the sailboats as the rooms draw instances.
    void draw(render::Device& device, ModelLibrary& models, TextureLibrary& textures, const Frustum& frustum,
              const Camera& camera, const ObjectDetail& detail) const;

    // InstanceSource: the bridges, ferries and train cars in `room`.
    void instancesIn(int room, std::vector<phys::Instance*>& out) const override;

    const std::vector<Sailboat>& sailboats() const { return m_sailboats; }
    const std::vector<Bridge>& bridges() const { return m_bridges; }
    const std::vector<Train>& trains() const { return m_trains; }
    const std::vector<Ferry>& ferries() const { return m_ferries; }
    // The path sets the managers loaded.
    const std::vector<std::string>& loadedPathSets() const { return m_loaded; }

    // Exposed for tests.
    void resetBridgeState(Bridge& b);
    void updateBridge(Bridge& b, float dt);
    void repositionBridge(Bridge& b);
    bool triggerBridge(std::size_t i);
    void updateTrain(Train& t, float dt);
    void updateTrainCar(TrainCar& c, float dt);
    bool inStation(const Train& t) const;
    void resetTrain(Train& t);

private:
    void loadSailboats(const vfs::Vfs& vfs, const city::PathSet& set, fx::Rand& random);
    void loadBridges(const vfs::Vfs& vfs, const city::PathSet& set, std::vector<std::uint16_t>* roomFlags);
    void loadTrains(const vfs::Vfs& vfs, const city::PathSet& set);
    void loadFerries(const vfs::Vfs& vfs, const city::PathSet& set, fx::Rand& random);
    std::unique_ptr<GizmoBody> makeBody(const std::string& model, bool yOnly) const;
    // dgUnhitBangerInstance::Init: the placement moved to the CG.
    void placeBanger(GizmoBody& body, const Mat34& placement) const;
    void moveToRoom(GizmoBody& body, int hint);
    void updateRoom(GizmoBody& body);
    void updateFerry(Ferry& f, float dt);
    void updateSailboat(Sailboat& s, float dt);

    const bangers::BangerDataLibrary& m_data;
    const bangers::BangerSet& m_bounds;
    const phys::Level* m_level = nullptr;
    std::vector<Sailboat> m_sailboats;
    std::vector<Bridge> m_bridges;
    std::vector<Train> m_trains;
    std::vector<Ferry> m_ferries;
    std::vector<std::vector<GizmoBody*>> m_rooms;
    std::vector<std::string> m_loaded;
};

// mmGame::InitGizmos for a session: the gizmos (reset, as mmGame::Reset
// does before the race starts), and the parked cars added to `bangers`
// (gizParkedCarMgr: in the managers' order, after the trains and ferries).
// `level` finds rooms; `city`'s level room flags get the bridges' 0x10.
std::unique_ptr<Gizmos> initGizmos(const vfs::Vfs& vfs, city::CityData& city, const RaceConfig& config,
                                   bool multiplayer, bangers::BangerSet& bangers,
                                   const bangers::BangerDataLibrary& data, const phys::Level* level,
                                   fx::Rand& random);

} // namespace mm2::game::world
