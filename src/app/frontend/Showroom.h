#pragma once

// The garage's turning 3D car (MM2 VehicleSelectBase with one asDofCS and
// one mmVehicleForm per car, seen through MenuManager's frontend camera).
// See docs/frontend.md, "Garage".

#include "asset/VehicleModel.h"
#include "game/ModelLibrary.h"
#include "game/TextureLibrary.h"
#include "render/Projection.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace mm2::app::frontend {

class Showroom {
public:
    Showroom(render::Device& device, const vfs::Vfs& vfs);
    ~Showroom();
    Showroom(const Showroom&) = delete;
    Showroom& operator=(const Showroom&) = delete;

    // VehicleSelectBase::SetPick: the car shown and the camera distance it
    // eases to (the car's UIDist).
    void setCar(const std::string& baseName, float uiDistance);
    // The paint job (the CAR COLOR drop-down; mmVehicleForm::Cull reads it
    // every frame).
    void setPaintjob(int paintjob) { m_paintjob = paintjob; }
    // VehicleSelectBase::Update and asDofCS::Update: the camera distance
    // eases towards the goal at 21 units a second; the shown car turns about
    // Y at 1 rad/s (each car keeps its own angle).
    void update(float dt);

    // Draws the car into the camera's viewport (asCamera::SetViewport 0.05,
    // 0.115, 0.95 x 0.4 of the 640x480 screen) inside a scene pass.
    void draw(const render::UiLayout& layout);

    // MenuManager::Init's camera: vertical field of view 0.6 rad, projection
    // aspect 3.2, near 1, far 100.
    static constexpr float kFovY = 0.6f, kAspect = 3.2f, kNear = 1.0f, kFar = 100.0f;
    // The viewport in 640x480 pixels: x 32, y 55, 608 x 192 (fractions of
    // the screen truncated to whole pixels).
    static render::Rect viewport640();
    // asViewCS::UpdatePolar's camera matrix for a distance: azimuth 0,
    // incline 0.18 rad (VehicleSelectBase::Update), twist 0, then the view's
    // offset (0, 0.86, 0).
    static Mat34 cameraMatrix(float distance);
    // The camera distance after `dt` seconds of easing from `distance`
    // towards `goal` at 21 units a second, clamped at the goal.
    static float easeDistance(float distance, float goal, float dt);
    float distance() const { return m_distance; }

private:
    struct Car {
        std::optional<asset::VehicleModel> model;
        const game::GpuModel* gpu = nullptr;
        float angle = 0.0f; // asDofCS +0xb8: kept while another car is shown
    };
    Car* current();
    std::vector<asset::PkgMaterial> materials(const Car& car) const;

    render::Device& m_device;
    const vfs::Vfs& m_vfs;
    std::unique_ptr<game::TextureLibrary> m_textures;
    std::unique_ptr<game::ModelLibrary> m_models;
    std::map<std::string, Car, std::less<>> m_cars;
    std::string m_car;
    int m_paintjob = 0;
    // MenuManager::Init sets the view's distance to 10; the goal starts at 10
    // too, so the first car slides in to its UIDist.
    float m_distance = 10.0f;
    float m_goal = 10.0f;
};

} // namespace mm2::app::frontend
