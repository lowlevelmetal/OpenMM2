#pragma once

#include "phys/TrailerJoint.h"
#include "phys/World.h"
#include "phys/vehicle/Drivetrain.h"
#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "phys/vehicle/Wheel.h"

#include <array>
#include <memory>
#include <optional>

namespace mm2::phys {

class CarSim;

// Geometry of a trailer in its own model space, which is also its rigid
// body's frame: vehTrailer has no centre of gravity field, its InertialCS
// sits at the model origin.
//   * wheels from geometry/<car>_trailer_twhl0..3.mtx (pivot rows as for
//     cars). TWHL4/TWHL5 (vpcentury's second axle) are not simulated: MM2
//     only records their offset from TWHL2/TWHL3 (vehCarSim
//     TrailerBackBackLeft/RightWheelPosDiff, named in mm2hook's headers) to
//     draw them.
//   * body box from bound/<car>_trailer_bound.bnd.
//   * the hitch pivots: vehCarModel::GetTrailerHitch reads the tractor's
//     geometry/<car>_trailer_hitch.mtx (model space; MM2 builds no trailer
//     without it), vehTrailerInstance::GetTrailerHitch the trailer's
//     geometry/<car>_trailer_trailer_hitch.mtx.
struct TrailerGeometry {
    std::array<WheelGeometry, 4> wheels; // TWHL0 FL, TWHL1 FR, TWHL2 BL, TWHL3 BR
    Aabb body;
    // bound/<car>_trailer_bound.bnd (the trailer's collision bound).
    std::optional<GeometryData> bound;
    std::optional<Vec3> carHitch;
    std::optional<Vec3> trailerHitch;
};

// OpenMM2 switches between MM2's exact behaviour and corrections of it (see
// docs/physics.md, "Trailers").
struct TrailerOptions {
    // vehTrailer::Init's static wheel loads exactly as MM2 computes them.
    // Off by default: MM2 swaps the hitch's and the axle's shares, which
    // leaves vpcentury's trailer on its bump stops (see setStaticLoads).
    bool mm2StaticLoads = false;
    // TrailerJoint::mm2ForceRotation (MM2's behaviour, on by default).
    bool mm2ForceRotation = true;
};

// vehTrailer (Midtown Madness 2, verified against the build 3393 code): the
// semi trailers of vpsemi and vpcentury. Its own rigid body, four wheels
// each in a free drivetrain, the back wheels taking the tractor's inputs,
// and a TrailerJoint (dgTrailerJoint) to the tractor, updated at the end of
// each sample (vehTrailer::Update). Add the trailer to the World after the
// tractor, so that the joint sees both bodies' forces for the next sample.
class Trailer final : public BodyController {
public:
    Trailer() = default;
    Trailer(const Trailer&) = delete;
    Trailer& operator=(const Trailer&) = delete;

    // vehTrailer::Init (with the trailer setup of vehCar::Init), then
    // reset(). Changes the tractor's wheel loads (vehWheel::AddNormalLoad),
    // so init the tractor first and only once.
    void init(const TrailerParams& params, const TrailerJointParams& joint, const TrailerGeometry& geometry,
              CarSim& tractor, const TrailerOptions& options = {});
    // vehTrailer::Reset: the trailer in line behind the tractor's current
    // pose, hitched, at rest.
    void reset();
    // Adds the trailer body to the world (call after the tractor's body was
    // added).
    void addTo(World& world);
    void removeFrom(World& world);

    // vehTrailer::BottomedOut: the number of wheels that bottomed out this
    // sample.
    int bottomedOut() const;
    // vehTrailer::RequiresTerrainCollision: as the car's (CarSim), with the
    // trailer's body and wheels.
    bool requiresTerrainCollision() const;
    // vehTrailer::SetCarHitchOffset / SetTrailerHitchOffset: copy the
    // trailer's hitch offsets into the joint (tuning callbacks).
    void setCarHitchOffset();
    void setTrailerHitchOffset();

    // The trailer's model matrix (its InertialCS).
    Mat34 modelMatrix() const { return body.ics.matrix; }
    // Distance between the two hitch points after the last sample, before
    // the joint's FreeRange correction.
    float hitchGap() const { return joint.gap.mag(); }
    // Diagnostic: the trailer's yaw relative to the tractor (rad, positive
    // when the trailer's tail points to the tractor's right, +X).
    float hitchAngle() const;

    void beforeIntegrate(Body& body, float dt, const World& world) override;
    void afterIntegrate(Body& body, float dt, const World& world) override;

    // dgTrailerJoint::Update's debug key: in the frame Ctrl+B goes down
    // (ioKeyboard's state, global like it), every hitch still holding
    // breaks. The game sets it once a frame.
    static inline bool breakKeyPressed = false;

    Body body;
    TrailerJoint joint;
    TrailerParams params;
    Vec3 carHitchOffset;     // tractor InertialCS space
    Vec3 trailerHitchOffset; // trailer model space
    Vec3 originOffset;       // CarHitchOffset - TrailerHitchOffset
    std::array<Drivetrain, 4> drivetrains;
    std::array<Wheel, 4> wheels;

private:
    // vehTrailer::Init's static loads: for a semi-trailer (both axles on the
    // same side of its origin) the trailer wheels' loads are set and the
    // hitch's share is added to the tractor's wheels (vehWheel::SetNormalLoad,
    // AddNormalLoad). `mm2`: MM2's values, else OpenMM2's correction.
    void setStaticLoads(bool mm2);

    CarSim* m_tractor = nullptr;
    std::unique_ptr<Bound> m_bound;
};

} // namespace mm2::phys
