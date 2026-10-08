#pragma once

#include "ai/World.h"
#include "asset/Ped.h"
#include "asset/VehicleModel.h"
#include "game/Camera.h"
#include "game/RaceConfig.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "game/VehicleRenderer.h"
#include "render/Device.h"
#include "vfs/Vfs.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace mm2::game {

// Draws the AI layer: ambient traffic (PKG cars posed from the rail state),
// pedestrians (skinned on the CPU from the posed skeleton) and the traffic
// signal glows.
class AiRenderer {
public:
    AiRenderer(render::Device& device, TextureLibrary& textures, ModelLibrary& models, const vfs::Vfs& vfs);

    // `physicalTransform` returns the transform of cars that the physics
    // simulation has taken over (null for cars on their rails). `lights` is
    // mmGame::InitWeather's light flag (evening, night or fog: the cars'
    // headlights and tail lights); the signals switch to their night glows
    // from the evening on (aiTrafficLightInstance::DrawGlow: time of day > 1).
    // `detail`: the Object Detail thresholds (lvlInstance::IsVisible).
    void draw(const ai::World& world, const Camera& camera, const Frustum& frustum, TimeOfDay time, bool lights,
              const ObjectDetail& detail, const std::function<const Mat34*(int)>& physicalTransform = {});

    struct Stats {
        int cars = 0;
        int peds = 0;
        int signals = 0;
    };
    const Stats& stats() const { return m_stats; }

private:
    struct CarModel {
        std::unique_ptr<asset::VehicleModel> model;
        std::map<int, std::unique_ptr<VehicleRenderer>> renderers; // by paint job
        int paintjobs = 1;
    };
    CarModel* carModel(const std::string& name);
    const asset::PedType* pedType(const std::string& name);
    void drawPed(const ai::Pedestrian& ped, const asset::PedType& type, const Camera& camera);
    void drawSignal(const ai::Signal& signal, const Camera& camera, bool nightGlows);

    render::Device& m_device;
    TextureLibrary& m_textures;
    ModelLibrary& m_models;
    const vfs::Vfs& m_vfs;
    std::map<std::string, std::unique_ptr<CarModel>> m_cars;
    std::map<std::string, std::optional<asset::PedType>> m_pedTypes;
    std::vector<Mat34> m_bones;
    std::vector<render::Vertex3D> m_skinned;
    ObjectDetail m_detail;
    Stats m_stats;
};

} // namespace mm2::game
