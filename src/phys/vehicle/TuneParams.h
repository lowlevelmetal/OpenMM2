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
// Defaults apply to fields a file does not set. The original constructor
// defaults are unknown; ours are the most common value among the retail files
// (see docs/physics.md), which is what a missing field most likely meant.

struct AeroParams {
    Vec3 angCDamp{0, 0, 0};    // AngCDamp
    Vec3 angVelDamp{0, 0, 0};  // AngVelDamp
    Vec3 angVel2Damp{0, 0, 0}; // AngVel2Damp
    float drag = 0.0f;         // Drag
    float down = 0.0f;         // Down
};

struct EngineParams {
    float angInertia = 1.0f;      // AngInertia (kg m^2)
    float maxHorsePower = 300.0f; // MaxHorsePower (hp)
    float idleRPM = 750.0f;       // IdleRPM
    float optRPM = 5800.0f;       // OptRPM: RPM of peak power
    float maxRPM = 8500.0f;       // MaxRPM: rev limit, reference RPM for gear speeds
    float gcl = 0.25f;            // GCL: gear change lag (s), no torque after a shift
};

struct TransmissionParams {
    // MM2 format.
    int manualNumGears = 7; // ManualNumGears: reverse + neutral + forward gears
    int autoNumGears = 6;   // AutoNumGears
    float reverse = 30.0f;  // Reverse: reverse gear speed at MaxRPM (mph)
    float low = 20.0f;      // Low: first gear speed at MaxRPM (mph)
    float high = 90.0f;     // High: top gear speed at MaxRPM (mph)
    float gearBias = 0.5f;  // GearBias: spacing of intermediate gears
    float upshiftBias = 0.05f;
    float downshiftBiasMin = 0.05f;
    float downshiftBiasMax = 0.3f;
    float gearChangeTime = 0.8f; // GearChangeTime: minimum time between automatic shifts (s)

    // Midtown Madness 1 format, still present in a few retail *_opp files:
    // explicit ratios per gear slot (0 reverse, 1 neutral, 2.. forward).
    bool hasExplicitRatios = false;
    int numGears = 0;                    // NumGears
    std::vector<float> gearRatios;       // GearRatios
    std::vector<float> manualGearRatios; // ManualGearRatios
    std::vector<float> upshiftRPM;       // UpshiftRPM
    std::vector<float> downshiftRPM;     // DownshiftRPM
    float downshiftBias = 1.55f;         // DownshiftBias (MM1 kickdown factor)
};

struct DrivetrainParams {
    float angInertia = 2000.0f;    // AngInertia (see kDrivetrainInertiaScale)
    float brakeDynamicCoef = 1.0f; // BrakeDynamicCoef
    float brakeStaticCoef = 1.2f;  // BrakeStaticCoef
};

struct WheelParams {
    float suspensionExtent = 0.2f;
    float suspensionLimit = 0.1f;
    float suspensionFactor = 1.0f;
    float suspensionDampCoef = 0.1f;
    float steeringLimit = 0.4f;
    float steeringOffset = 0.0f;
    float brakeCoef = 0.6f;
    float handbrakeCoef = 2.0f;
    float camberLimit = 0.0f;
    float wobbleLimit = 0.0f;
    float tireDispLimitLong = 0.125f;
    float tireDampCoefLong = 0.25f;
    float tireDragCoefLong = 0.02f;
    float tireDispLimitLat = 0.125f;
    float tireDampCoefLat = 0.25f;
    float tireDragCoefLat = 0.05f;
    float optimumSlipPercent = 0.15f;
    float staticFric = 3.0f;
    float slidingFric = 2.8f;
};

struct AxleParams {
    float torqueCoef = 0.0f; // TorqueCoef
    float dampCoef = 0.0f;   // DampCoef
};

struct CarSimParams {
    float mass = 1000.0f;
    Vec3 inertiaBox{2, 2, 4};
    Vec3 centerOfGravity{0, 0, 0};
    float boundFriction = 0.5f;
    float boundElasticity = 0.5f;
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
    float drift = 0.2f;
    float spin180 = 0.8f;
    float reverse180 = 2.617f;
};

struct StuckParams {
    float turn = 1.57f;
    float rotation = 0.0f;
    float translation = 0.1f;
    float timeThresh = 2.0f;
    float posThresh = 1.25f;
    float moveThresh = 1.75f;
};

struct CarDamageParams {
    float maxDamage = 300000.0f;
    float medDamage = 150000.0f;
    float impactThreshold = 1500.0f;
    float regenerateRate = 0.0f;
    Vec3 smokeOffset;
    Vec3 smokeOffset2;
    float textelDamageRadius = 2.7f;
    bool doublePivot = false;
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
