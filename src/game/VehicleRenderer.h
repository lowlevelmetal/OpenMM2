#pragma once

#include "asset/VehicleModel.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/RoomVisibility.h"
#include "game/TexelDamage.h"
#include "game/TextureLibrary.h"
#include "game/fx/LensFlares.h"
#include "game/fx/ParticleRenderer.h"

#include <array>
#include <functional>
#include <optional>
#include <set>

namespace mm2::game {

// What a vehicle looks like this frame: the chassis placement plus wheel
// animation, as produced by the simulation (or interpolated for remote cars).
struct VehiclePose {
    Mat34 body;                       // model space -> world
    std::array<float, 6> wheelSpin{}; // rotation about the axle, radians
    std::array<float, 6> wheelSteer{};// steering angle about the wheel's vertical axis
    std::array<float, 6> wheelDrop{}; // suspension: wheel centre offset along the body's up axis
    // When set, wheels are drawn with these world matrices (from the
    // simulation) instead of being derived from spin/steer/drop.
    std::array<Mat34, 6> wheelWorld{};
    std::array<bool, 6> wheelValid{};
    bool hasWheelWorld = false;
    // A traffic car the physics simulation has taken over (aiVehicleInstance
    // with an aiVehicleActive): its wheels are wheelWorld and its shadow is
    // laid on the ground first (aiVehicleInstance::Draw / DrawShadow).
    bool physical = false;
    // mmGame::InitWeather's light flag (evening, night or fog): the tail
    // lights glow and the headlights shine.
    bool headlights = false;
    // Brake input not zero (vehCarSim): tail and brake lights.
    bool brakeLights = false;
    // Reverse gear: reversing lights.
    bool reverseLights = false;
    // Traffic indicators lit this frame (aiVehicleInstance::DrawGlow): bit 1
    // the SLIGHT0 part, bit 2 SLIGHT1 (both: hazards).
    int indicators = 0;
    // Police lights (vehSiren) on, and how far their beams have turned
    // (radians; vehSiren::Update turns them 2.5 pi per second while on).
    bool siren = false;
    float sirenAngle = 0.0f;
};

// Draws a vehicle like MM2's vehCarModel: body, decal, breakable parts,
// reflections, fenders, wheels and hubs in the object pass; the ground
// shadow; and the glows (lights, headlight and siren beams).
class VehicleRenderer {
public:
    // Ground under a point: from -> to segment, returns the hit point and normal.
    using GroundProbe = std::function<bool(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal)>;

    // `bodyPart`/`wheelPrefix` select the part names: "BODY"/"WHL" for cars,
    // "TRAILER"/"TWHL" for semi trailers, which are drawn like
    // vehTrailerInstance (see drawTrailer).
    VehicleRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                    const asset::VehicleModel& model, int paintjob, std::string bodyPart = "BODY",
                    std::string wheelPrefix = "WHL");

    const asset::VehicleModel& model() const { return m_model; }
    void setPaintjob(int paintjob);

    // Object Detail thresholds (lvlInstance::IsVisible).
    void setDetail(const ObjectDetail& detail) { m_detail = detail; }
    // Ground for the shadow; without one no shadow is drawn.
    void setGroundProbe(GroundProbe probe) { m_probe = std::move(probe); }
    // cityLevel::GetEnvMap: refl_dc reflections on the high LOD body
    // ("Vehicle Reflections" option).
    void setReflections(bool on) { m_reflections = on; }

    // fxTexelDamage::ApplyDamage at a point in the car's model space with
    // TextelDamageRadius; resetDamage() repaints the car clean.
    void applyDamage(const Vec3& modelPoint, float radius);
    void resetDamage();
    bool hasTexelDamage() const { return m_texelDamage && m_texelDamage->active(); }
    // The paint job's material textures in order (fxShardManager's shards).
    std::vector<std::string> materialTextures() const;
    int paintjob() const { return m_paintjob; }

    // vehBreakableMgr: parts that fly off and stop being drawn. The pivot
    // is the part's placement in model space (vehBreakable::vehBreakable:
    // identity rotation at the pivot; vehBreakableMgr::Add / vehBreakable::Add
    // keep the parts in a list, in the order vehCarModel::Init adds them).
    struct Breakable {
        std::string part; // mesh part name, e.g. "BREAK0", "WHL2"
        Vec3 pivot;
    };
    // Manager A (vehBreakableMgr::Impact): the attached breakable part
    // (BREAK0-3, BREAK01/12/23/03, the paint job's VARIANT) whose pivot is
    // nearest `modelPoint`.
    std::optional<Breakable> nearestBreakable(const Vec3& modelPoint) const;
    // Manager B (vehCarModel::EjectOneshot), once until reattachAll():
    // the wheels, hubs and fenders a wrecked car loses at `mph`
    // (vehBreakableMgr::Get by id bit, vehBreakableMgr::EjectAll above
    // 100 mph).
    std::vector<Breakable> wreckParts(float mph, fx::Rand& rng);
    // vehBreakableMgr::Eject: the part stops being drawn; `banger` is the
    // hit banger instance it became (vehBreakable +0x44), if any.
    void detach(const std::string& part, std::optional<std::size_t> banger = {});
    // vehBreakableMgr::Reset calls the hit banger's Detach (it leaves the
    // world) for every ejected part when the parts go back on: `f` does that
    // (BangerSet::detachHit).
    void setEjectedPartReset(std::function<void(std::size_t)> f) { m_ejectedPartReset = std::move(f); }
    // vehCarModel::ClearDamage: everything back on (vehBreakableMgr::Reset
    // of both managers).
    void reattachAll();

    // Draws everything for the camera placed at `camera`.
    void draw(const VehiclePose& pose, const Mat34& camera);
    // The same with cityLevel::DrawRooms' room gates of the car's room
    // (RoomVisibility): the car itself in cityLevel_drawObjects, its shadow
    // and glows (vehCarModel::DrawShadow / DrawGlow, without IsVisible) in
    // cityLevel_drawShadows / cityLevel_drawLights.
    void draw(const VehiclePose& pose, const Mat34& camera, const RoomVisibility::Passes& passes);
    // The rooms the city listed for the view: draw() then keeps the car's
    // room (vehCar::Update: FindRoomId from the last one) and gates by it.
    // Not for the traffic renderers AiRenderer shares between cars.
    void setRooms(const RoomVisibility* rooms) { m_rooms = rooms; }

    // The level of detail at that camera; nullopt beyond NoDraw.
    std::optional<asset::Lod> lodFor(const VehiclePose& pose, const Mat34& camera) const;

    // Draw like aiVehicleInstance (ambient traffic) instead of vehCarModel:
    // no texel damage, decal, variant or fender parts; breakables always at
    // the high LOD; wheels only at the high LOD; tail light glows and one
    // white headlight glow pair (see VehicleRenderer.cpp).
    void setTraffic(bool traffic);
    // ltLight::DrawGlow's card size and colour scales (MM2 globals: 0.2 and
    // 0.95 after aiVehicleManager::Init, the single-player value; 0.2 and
    // 0.6 after a vehSiren is constructed later, as network cars are).
    static void setLightGlowScales(float size, float color);
    // vehSiren::Draw's lens flares (ltLensFlare::Draw): while a target is
    // set, the siren lights' flares for that view go into `out`, to be drawn
    // after the scene (fx::drawLensFlares). Null turns them off.
    static void setLensFlareTarget(const Mat44* viewProj, float aspect, std::vector<fx::LensFlareQuad>* out);

private:
    struct Light {
        Vec3 position; // model space
        Vec3 color;    // the part's material colour
    };
    void drawPart(std::string_view part, asset::Lod lod, const Mat34& transform, const MeshDrawOptions& options,
                  bool live = true);
    void drawCar(const VehiclePose& pose, asset::Lod lod);
    void drawTraffic(const VehiclePose& pose, asset::Lod lod);
    void drawTrailer(const VehiclePose& pose, asset::Lod lod);
    void drawReflection(const Mat34& body);
    // The world matrix wheel `i` (0-5) is drawn with, if the car has it.
    std::optional<Mat34> wheelMatrix(const VehiclePose& pose, std::size_t i) const;
    void drawShadow(const VehiclePose& pose);
    void drawGlows(const VehiclePose& pose, const Mat34& camera);
    void addLightGlow(fx::ParticleRenderer& cards, const Vec3& position, const Vec3& direction, const Vec3& color,
                      const Mat34& camera);
    std::optional<Mat34> shadowMatrix(const Mat34& body) const;

    render::Device& m_device;
    TextureLibrary& m_textures;
    const asset::VehicleModel& m_model;
    const GpuModel* m_gpu = nullptr;
    int m_paintjob = 0;
    std::string m_bodyPart, m_wheelPrefix;
    // The paint job's materials as drawn at H and M ("_dmg" textures replaced
    // by their clean counterparts, fxTexelDamage) and as stored (L and VL).
    std::vector<asset::PkgMaterial> m_live, m_paint;
    ObjectDetail m_detail;
    GroundProbe m_probe;
    bool m_reflections = true;
    float m_radius = 1.0f; // the body's bounding radius
    std::array<std::optional<Light>, 2> m_headlights;
    std::vector<Light> m_sirens;
    std::optional<Vec3> m_fenderOffset; // fndr0 pivot relative to wheel 0
    fx::ParticleRenderer m_cards;
    std::optional<fx::LensFlare> m_flare; // vehSiren's ltLensFlare(20)
    std::unique_ptr<TexelDamage> m_texelDamage;
    std::set<std::string> m_detached;
    std::vector<std::size_t> m_ejectedBangers; // in ejection order
    std::function<void(std::size_t)> m_ejectedPartReset;
    bool m_wreckEjected = false;
    bool m_traffic = false;
    bool m_trailer = false; // a vehTrailerInstance ("TRAILER" body)
    const RoomVisibility* m_rooms = nullptr;
    int m_room = 0; // lvlInstance's room (vehCar::Update)
};

// lvlInstance::DrawPhysics: a shadow's matrix on the ground under `body`
// (nullopt without ground or on a steep slope).
std::optional<Mat34> groundShadowMatrix(const Mat34& body, const VehicleRenderer::GroundProbe& probe);
// aiVehicleInstance::DrawShadow's placement of a traffic car's shadow;
// `physical`: the car has a body (aiVehicleActive).
Mat34 trafficShadowMatrix(const Mat34& body, bool physical, const VehicleRenderer::GroundProbe& probe);

} // namespace mm2::game
