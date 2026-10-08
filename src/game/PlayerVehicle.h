#pragma once

#include "asset/VehicleModel.h"
#include "game/VehicleRenderer.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"
#include "phys/vehicle/Controls.h"
#include "phys/vehicle/Trailer.h"
#include "vfs/Vfs.h"

#include <memory>
#include <string>

namespace mm2::game {

// A simulated car: its model and pivots, tune parameters
// (tune/vehicle/<base>.vehCarSim, .vehGyro, .vehStuck, .vehCarDamage), body
// collision box (bound/<base>_bound.bbnd) and the CarSim built from them.
// Semis (vehTrailer data present) also get their trailer.
class SimVehicle {
public:
    // `tuneSuffix` selects e.g. the opponent tune ("_opp") when present.
    static std::unique_ptr<SimVehicle> load(const vfs::Vfs& vfs, std::string_view baseName, std::string* error,
                                            std::string_view tuneSuffix = {});

    const asset::VehicleModel& model() const { return m_model; }
    phys::CarSim& sim() { return m_sim; }
    const phys::CarSim& sim() const { return m_sim; }

    void addTo(phys::World& world);
    void removeFrom(phys::World& world);
    // Places the car's model origin at `model` and resets its state.
    void reset(const Mat34& model);

    // Applies pedal input through the original's automatic-reverse logic
    // (mmGame::UpdateSteeringBrakes). After hold() it first makes the car
    // drivable again in first gear (vehCar::SetDrivable(1, ...)).
    void drive(const phys::PedalInput& input);
    // Held on the start line (vehCar::SetDrivable(0, 1) and
    // vehCar::PreUpdate): full brakes and neutral, so the throttle revs the
    // engine freely; the steering and handbrake stay the player's.
    void hold(const phys::PedalInput& input);
    bool reversing() const;

    // Pose for rendering (body and wheel matrices from the simulation).
    VehiclePose pose() const;

    // Semi trailer (vpsemi, vpcentury), if the car has one.
    const asset::VehicleModel* trailerModel() const { return m_trailerModel.get(); }
    VehiclePose trailerPose() const;

private:
    asset::VehicleModel m_model;
    phys::CarSim m_sim;
    phys::ArcadeControls m_controls;
    bool m_held = false;
    std::unique_ptr<phys::Trailer> m_trailer;
    std::unique_ptr<asset::VehicleModel> m_trailerModel;
};

} // namespace mm2::game
