#pragma once

#include "phys/Joint3Dof.h"
#include "phys/World.h"
#include "phys/vehicle/Drivetrain.h"
#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "phys/vehicle/Wheel.h"

#include <array>

namespace mm2::phys {

class CarSim;

// Geometry of a trailer in its own model space: wheels from
// geometry/<car>_trailer_twhl0..3.mtx (pivot rows as for cars) and the body
// box from bound/<car>_trailer_bound.bnd.
//
// TWHL4/TWHL5 (vpcentury's second rear axle) are not simulated: MM2 only
// records their offset from TWHL2/TWHL3 (vehCarSim
// TrailerBackBackLeft/RightWheelPosDiff, named in mm2hook's headers) to draw
// them, so they are visual copies of the back wheels.
struct TrailerGeometry {
    std::array<WheelGeometry, 4> wheels; // TWHL0 FL, TWHL1 FR, TWHL2 BL, TWHL3 BR
    Aabb body;
    // Trailer model space. vehTrailer has no CG field; MM1's mmCar::Init
    // passes the TRAILER_H mesh's box centre (DLPTemplate::GetCentroid) to
    // mmTrailer::Init as the trailer's centre.
    Vec3 centerOfGravity;
};

// vehTrailer + dgTrailerJoint (the semi trailers of vpsemi and vpcentury).
//
// Port of MM1's mmTrailer (Open1560 game.asm): its own rigid body, four
// wheels each in a free drivetrain, wheel inputs copied from the tractor
// every update (mmTrailer::Update), linked to the tractor by a Joint3Dof
// (see phys/Joint3Dof.h) set up as in mmCar::Init. The joint integrates both
// bodies; World runs it after the free bodies' integration.
class Trailer final : public BodyController {
public:
    Trailer() = default;
    Trailer(const Trailer&) = delete;
    Trailer& operator=(const Trailer&) = delete;

    void init(const TrailerParams& params, const TrailerJointParams& joint, const TrailerGeometry& geometry,
              CarSim& tractor);
    // mmTrailer::Reset: places the trailer behind the tractor's current pose,
    // hitched (MM2 adaptation: the trailer model has its own origin, so the
    // hitch points are made to coincide).
    void reset();
    // Adds the trailer body and joint to the world (call after the tractor's
    // body was added).
    void addTo(World& world);
    void removeFrom(World& world);

    // mmTrailer::RestoreImpactParams / SetHackedImpactParams.
    void restoreImpactParams();
    void setHackedImpactParams();

    Mat34 modelMatrix() const;
    // Distance between the two hitch points after the last sample (the
    // original's global `discrepancy`).
    float hitchGap() const { return joint.discrepancy.mag(); }

    void beforeIntegrate(Body& body, float dt, const World& world) override;
    void afterIntegrate(Body& body, float dt, const World& world) override;

    Body body;
    Joint3Dof joint;
    TrailerParams params;
    TrailerJointParams jointParams;
    Vec3 centerOfGravity;
    float boundElasticity = 0.0f;
    float boundFriction = 0.0f;
    std::array<Drivetrain, 4> drivetrains;
    std::array<Wheel, 4> wheels;

private:
    CarSim* m_tractor = nullptr;
};

} // namespace mm2::phys
