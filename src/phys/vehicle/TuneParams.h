#pragma once

#include "core/Math.h"
#include "data/DatFile.h"

#include <optional>
#include <string>
#include <vector>

namespace mm2::phys {

// Parameters of the Angel vehicle classes as read from the tune files
// (tune/vehicle/<car>.vehCarSim, .vehGyro, .vehStuck, .vehCarDamage,
// .vehTrailer, .dgTrailerJoint). Field names match the files exactly.
//
// Defaults apply to fields a file does not set; they are the original
// constructors' values (verified against the build 3393 code).

struct AeroParams {
    Vec3 angCDamp{0, 0, 0};    // AngCDamp
    Vec3 angVelDamp{0, 0, 0};  // AngVelDamp
    Vec3 angVel2Damp{0, 0, 0}; // AngVel2Damp
    float drag = 0.0f;         // Drag
    float down = 0.0f;         // Down
};

struct EngineParams {
    float angInertia = 1.0f;      // AngInertia (kg m^2)
    float maxHorsePower = 200.0f; // MaxHorsePower (hp)
    float idleRPM = 750.0f;       // IdleRPM: clutch opens below it, engine-braking zero
    float optRPM = 5000.0f;       // OptRPM: RPM of peak power, reference RPM for gear speeds
    float maxRPM = 8000.0f;       // MaxRPM: rev limit
    float gcl = 0.25f;            // GCL: gear change lag (s), no torque after a shift
};

struct TransmissionParams {
    int manualNumGears = 7; // ManualNumGears: reverse + neutral + forward gears
    int autoNumGears = 6;   // AutoNumGears
    float reverse = 20.0f;  // Reverse: reverse gear speed at OptRPM (mph)
    float low = 20.0f;      // Low: first gear speed at OptRPM (mph)
    float high = 75.0f;     // High: top gear speed at OptRPM (mph)
    float gearBias = 0.5f;  // GearBias: pushes the intermediate gears towards High
    float upshiftBias = 0.05f;
    float downshiftBiasMin = 0.05f;
    float downshiftBiasMax = 0.3f;
    float gearChangeTime = 0.8f; // GearChangeTime: minimum time in gear before an automatic shift (s)
    // Midtown Madness 1 fields (GearRatios, UpshiftRPM, ...) left in a few
    // retail *_opp files are not read by MM2's vehTransmission.
};

struct DrivetrainParams {
    float angInertia = 5000.0f;    // AngInertia: damping of the wheel speed step (see Drivetrain)
    float brakeDynamicCoef = 1.0f; // BrakeDynamicCoef
    float brakeStaticCoef = 1.2f;  // BrakeStaticCoef
};

struct WheelParams {
    float suspensionExtent = 0.2f; // droop travel (m)
    float suspensionLimit = 0.1f;  // compression travel (m)
    float suspensionFactor = 1.0f; // spring progression (>= 0.75)
    float suspensionDampCoef = 0.1f;
    float steeringLimit = 0.39f; // rad at full lock
    float steeringOffset = 0.0f; // Ackermann-style inner/outer difference
    float brakeCoef = 1.0f;
    float handbrakeCoef = 1.0f;
    float camberLimit = -1.0f;
    float wobbleLimit = 0.0f;
    float tireDispLimitLong = 0.075f;
    float tireDampCoefLong = 0.25f;
    float tireDragCoefLong = 0.02f;
    float tireDispLimitLat = 0.075f;
    float tireDampCoefLat = 0.25f;
    float tireDragCoefLat = 0.05f;
    float optimumSlipPercent = 0.14f;
    float staticFric = 2.0f;
    float slidingFric = 1.9f;
};

struct AxleParams {
    float torqueCoef = 0.0f; // TorqueCoef
    float dampCoef = 0.0f;   // DampCoef
};

struct CarSimParams {
    float mass = 2000.0f;
    Vec3 inertiaBox{2, 1, 3};
    // vehCarSim adds R * CenterOfGravity to the body position to get the
    // model's origin, so the centre of mass sits at -CenterOfGravity in
    // model space.
    Vec3 centerOfGravity{0, 0, 0};
    float boundFriction = 0.3f;
    float boundElasticity = 0.2f;
    int drivetrainType = 0; // 0 rear, 1 front, 2 all-wheel drive
    float sssValue = 1.0f;
    float sssThreshold = 0.0f;
    float carFrictionHandling = 1.0f;
    AeroParams aero;
    EngineParams engine;
    TransmissionParams trans;
    DrivetrainParams drivetrain;
    DrivetrainParams freetrain;
    WheelParams wheelFront;
    WheelParams wheelBack;
    AxleParams axleFront;
    AxleParams axleBack;
};

struct GyroParams {
    float drift = 0.0f;
    float spin180 = 0.0f;
    float reverse180 = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

struct StuckParams {
    float turn = 1.57f;
    float rotation = 0.39f;
    float translation = 0.1f;
    float timeThresh = 0.3f;
    float posThresh = 1.25f;
    float moveThresh = 1.75f;
};

struct CarDamageParams {
    float maxDamage = 1000000.0f;
    float medDamage = 500000.0f;
    float impactThreshold = 100.0f;
    float regenerateRate = 0.0f;
    Vec3 smokeOffset{0.0f, 0.8f, -1.8f};
    Vec3 smokeOffset2;
    float textelDamageRadius = 0.4f;
    bool doublePivot = false;
    bool mirrorPivot = false;
};

struct TrailerParams {
    float mass = 2000.0f;
    Vec3 inertiaBox{2.5f, 0.6f, 12.0f};
    // Hitch points, when the .vehTrailer file carries them (vpsemi does;
    // vpcentury keeps them in its .dgTrailerJoint as Offset0/Offset1).
    std::optional<Vec3> carHitchOffset;     // tractor model space
    std::optional<Vec3> trailerHitchOffset; // trailer model space
    WheelParams wheelFront;
    WheelParams wheelBack;
    // Drivetrain block (vpsemi); absent -> MM1's free drivetrain constants.
    std::optional<DrivetrainParams> drivetrain;
};

// dgTrailerJoint. The names match MM1's Joint3Dof parameters:
// RestoreForce/DampConst/DampLinear = SetFriction*(restore, const, linear),
// LeanLimit/LimitElasticityLean = SetLeanLimit, LimitElasticityRoll =
// SetRollLimit's elasticity (MM1 mmCar::Init passes exactly vpcentury's old
// tune/vpcentury.dgTrailerJoint values).
struct TrailerJointParams {
    Vec3 offset0;            // hitch on the tractor (tractor model space)
    Vec3 offset1;            // hitch on the trailer (trailer model space)
    bool hasOffsets = false; // Offset0/Offset1 present in the file
    float forceLimit = 0.0f; // 0 = unbreakable
    int jointStatus = 2;
    float restoreForceLean = 2.0f;
    float dampConstLean = 2.0f;
    float dampLinearLean = 0.9f;
    float restoreForceRoll = 2.0f;
    float dampConstRoll = 0.1f;
    float dampLinearRoll = 2.0f;
    float leanLimit = 3.0f;
    float limitElasticityLean = 0.0f;
    float limitElasticityRoll = 0.0f;
    // MM2 dgTrailerJoint has NegativeRollLimit/PositiveRollLimit members, but
    // no retail file sets them; absent -> Joint3Dof::Init's -pi/+pi.
    std::optional<float> negativeRollLimit;
    std::optional<float> positiveRollLimit;
    // MM2 additions with no MM1 counterpart (vpsemi only): FreeRange,
    // FreeLean, FreeRoll. Semantics inferred, see docs/physics.md.
    float freeRange = 0.0f;
    float freeLean = 0.0f;
    float freeRoll = 0.0f;
};

// Loaders take the top-level block ("vehCarSim { ... }"). Fields the Angel
// classes do not know (MM1-era leftovers such as RedistHeight) are skipped,
// as the original parser does; their names are appended to `ignored`.
bool loadCarSimParams(const data::DatNode& block, CarSimParams& out,
                      std::vector<std::string>* ignored = nullptr);
bool loadGyroParams(const data::DatNode& block, GyroParams& out);
bool loadStuckParams(const data::DatNode& block, StuckParams& out);
bool loadCarDamageParams(const data::DatNode& block, CarDamageParams& out);
bool loadTrailerParams(const data::DatNode& block, TrailerParams& out);
bool loadTrailerJointParams(const data::DatNode& block, TrailerJointParams& out);
void loadWheelParams(const data::DatNode& block, WheelParams& out);

} // namespace mm2::phys
