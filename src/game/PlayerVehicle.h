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
    // `tuneSuffix` selects a tune variant (e.g. "_opp") when present. MM2
    // itself never loads one (OpenMM2 option, unused by the game).
    static std::unique_ptr<SimVehicle> load(const vfs::Vfs& vfs, std::string_view baseName, std::string* error,
                                            std::string_view tuneSuffix = {}, bool player = false,
                                            bool trailer = true);
    // The local player's car (mmPlayer::Init): as load(), with the
    // player-only rules (the police car simulated as vpmustang99). `trailer`
    // is vehCar::Init's trailer flag: mmPlayer::Init clears it in
    // multiplayer cruise and Cops and Robbers.
    static std::unique_ptr<SimVehicle> loadPlayer(const vfs::Vfs& vfs, std::string_view baseName,
                                                  std::string* error, bool trailer = true);

    const asset::VehicleModel& model() const { return m_model; }
    phys::CarSim& sim() { return m_sim; }
    const phys::CarSim& sim() const { return m_sim; }

    void addTo(phys::World& world);
    void removeFrom(phys::World& world);
    // Places the car's model origin at `model` and resets its state
    // (OpenMM2's placement; the game's spawns use setResetPos and reset()).
    void reset(const Mat34& model);
    // vehCarSim::SetResetPos and the reset rotation (vehCarSim +0x250), as
    // the game sets them for a start, a post or a checkpoint: where reset()
    // puts the car. The transform form takes the position and the turn about
    // Y of a spawn built by Mat34::rotationY (phys::resetRotationOf).
    void setResetPos(const Vec3& position, float rotation);
    void setResetPos(const Mat34& spawn);
    // vehCar::Reset: the car back at its reset position (CarSim::reset),
    // its trailer behind it (vehTrailer::Reset), everything at rest.
    void reset();
    // mmSingleCircuit::HitWaterHandler, mmGameMulti::HitWaterHandler: the
    // car reset at `at` (SetResetPos, the rotation, mmPlayer::Reset), then
    // the reset position and rotation put back. The game puts the position
    // back through SetResetPos, which adds CenterOfGravity again, so every
    // such respawn moves the start a later reset() uses by CenterOfGravity.
    void respawnAt(const Mat34& at);

    // Applies pedal input through the original's automatic-reverse logic
    // (mmGame::UpdateSteeringBrakes). After hold() it first makes the car
    // drivable again in first gear (vehCar::SetDrivable(1, ...)).
    void drive(const phys::PedalInput& input);
    // Held on the start line (vehCar::SetDrivable(0, 1) and
    // vehCar::PreUpdate): full brakes and neutral, so the throttle revs the
    // engine freely; the steering and handbrake stay the player's.
    void hold(const phys::PedalInput& input);
    // The pedal handling's state (the AUTO REVERSE option, the swapped
    // pedals that the transmission keys reset).
    phys::ArcadeControls& controls() { return m_controls; }
    bool reversing() const;

    // Pose for rendering (body and wheel matrices from the simulation).
    VehiclePose pose() const;

    // Semi trailer (vpsemi, vpcentury), if the car has one.
    const phys::Trailer* trailer() const { return m_trailer.get(); }
    const asset::VehicleModel* trailerModel() const { return m_trailerModel.get(); }
    phys::Trailer* trailer() { return m_trailer.get(); }
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
