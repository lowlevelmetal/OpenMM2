#pragma once

#include "asset/VehicleModel.h"
#include "game/MeshDraw.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"

#include <array>

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
    bool headlights = false;
    bool brakeLights = false;
    bool reverseLights = false;
};

// Draws a vehicle's parts (body, wheels, shadow, light glows) with a paint job.
class VehicleRenderer {
public:
    // `bodyPart`/`wheelPrefix` select the part names: "BODY"/"WHL" for cars,
    // "TRAILER"/"TWHL" for semi trailers.
    VehicleRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models,
                    const asset::VehicleModel& model, int paintjob, std::string bodyPart = "BODY",
                    std::string wheelPrefix = "WHL");

    const asset::VehicleModel& model() const { return m_model; }
    void setPaintjob(int paintjob);

    // Picks the level of detail from the distance to `eye`.
    void draw(const VehiclePose& pose, const Vec3& eye);

private:
    void drawPart(std::string_view part, asset::Lod lod, const Mat34& transform, const MeshDrawOptions& options);

    render::Device& m_device;
    TextureLibrary& m_textures;
    const asset::VehicleModel& m_model;
    const GpuModel* m_gpu = nullptr;
    int m_paintjob = 0;
    std::string m_bodyPart, m_wheelPrefix;
    // The paint job's materials as drawn: "_dmg" textures replaced by their
    // clean counterparts (see the constructor).
    std::vector<asset::PkgMaterial> m_materials;
};

} // namespace mm2::game
