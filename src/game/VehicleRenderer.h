#pragma once

#include "asset/VehicleModel.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/fx/ParticleRenderer.h"

#include <array>
#include <functional>
#include <optional>

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
    // mmGame::InitWeather's light flag (evening, night or fog): the tail
    // lights glow and the headlights shine.
    bool headlights = false;
    // Brake input not zero (vehCarSim): tail and brake lights.
    bool brakeLights = false;
    // Reverse gear: reversing lights.
    bool reverseLights = false;
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
    // "TRAILER"/"TWHL" for semi trailers.
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

    // Draws everything for the camera placed at `camera`.
    void draw(const VehiclePose& pose, const Mat34& camera);

    // The level of detail at that camera; nullopt beyond NoDraw.
    std::optional<asset::Lod> lodFor(const VehiclePose& pose, const Mat34& camera) const;

private:
    struct Light {
        Vec3 position; // model space
        Vec3 color;    // the part's material colour
    };
    void drawPart(std::string_view part, asset::Lod lod, const Mat34& transform, const MeshDrawOptions& options,
                  bool live = true);
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
};

} // namespace mm2::game
