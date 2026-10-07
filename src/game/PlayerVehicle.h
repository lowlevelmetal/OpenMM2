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

    // Applies pedal input through the original's automatic-reverse logic.
    void drive(const phys::PedalInput& input) { m_controls.apply(m_sim, input); }
    // Held on the start line: full brakes in drive, without the automatic
    // reverse (which would back the car away while the brake is held).
    void hold(float steering);
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
    std::unique_ptr<phys::Trailer> m_trailer;
    std::unique_ptr<asset::VehicleModel> m_trailerModel;
};

} // namespace mm2::game
