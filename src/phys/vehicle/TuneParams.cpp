#include "phys/vehicle/TuneParams.h"

#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace mm2::phys {
namespace {

using data::DatNode;

void noteIgnored(const DatNode& block, std::initializer_list<std::string_view> known, std::string_view prefix,
                 std::vector<std::string>* ignored) {
    if (!ignored)
        return;
    for (const auto& c : block.children)
        if (std::ranges::find(known, std::string_view(c.name)) == known.end())
            ignored->push_back(std::string(prefix) + c.name);
}

void loadAero(const DatNode& b, AeroParams& a) {
    b.read("AngCDamp", a.angCDamp);
    b.read("AngVelDamp", a.angVelDamp);
    b.read("AngVel2Damp", a.angVel2Damp);
    b.read("Drag", a.drag);
    b.read("Down", a.down);
}

void loadEngine(const DatNode& b, EngineParams& e) {
    b.read("AngInertia", e.angInertia);
    b.read("MaxHorsePower", e.maxHorsePower);
    b.read("IdleRPM", e.idleRPM);
    b.read("OptRPM", e.optRPM);
    b.read("MaxRPM", e.maxRPM);
    b.read("GCL", e.gcl);
}

void loadTrans(const DatNode& b, TransmissionParams& t) {
    b.read("ManualNumGears", t.manualNumGears);
    b.read("AutoNumGears", t.autoNumGears);
    b.read("Reverse", t.reverse);
    b.read("Low", t.low);
    b.read("High", t.high);
    b.read("GearBias", t.gearBias);
    b.read("UpshiftBias", t.upshiftBias);
    b.read("DownshiftBiasMin", t.downshiftBiasMin);
    b.read("DownshiftBiasMax", t.downshiftBiasMax);
    b.read("GearChangeTime", t.gearChangeTime);
}

void loadDrivetrain(const DatNode& b, DrivetrainParams& d) {
    b.read("AngInertia", d.angInertia);
    b.read("BrakeDynamicCoef", d.brakeDynamicCoef);
    b.read("BrakeStaticCoef", d.brakeStaticCoef);
}

void loadAxle(const DatNode& b, AxleParams& a) {
    b.read("TorqueCoef", a.torqueCoef);
    b.read("DampCoef", a.dampCoef);
}

} // namespace

void loadWheelParams(const DatNode& b, WheelParams& w) {
    b.read("SuspensionExtent", w.suspensionExtent);
    b.read("SuspensionLimit", w.suspensionLimit);
    b.read("SuspensionFactor", w.suspensionFactor);
    b.read("SuspensionDampCoef", w.suspensionDampCoef);
    b.read("SteeringLimit", w.steeringLimit);
    b.read("SteeringOffset", w.steeringOffset);
    b.read("BrakeCoef", w.brakeCoef);
    b.read("HandbrakeCoef", w.handbrakeCoef);
    b.read("CamberLimit", w.camberLimit);
    b.read("WobbleLimit", w.wobbleLimit);
    b.read("TireDispLimitLong", w.tireDispLimitLong);
    b.read("TireDampCoefLong", w.tireDampCoefLong);
    b.read("TireDragCoefLong", w.tireDragCoefLong);
    b.read("TireDispLimitLat", w.tireDispLimitLat);
    b.read("TireDampCoefLat", w.tireDampCoefLat);
    b.read("TireDragCoefLat", w.tireDragCoefLat);
    b.read("OptimumSlipPercent", w.optimumSlipPercent);
    b.read("StaticFric", w.staticFric);
    b.read("SlidingFric", w.slidingFric);
}

const data::DatSchema& carSimSchema() {
    using R = data::DatRecord;
    using T = R::Type;
    static const data::DatSchema schema = [] {
        auto floats = [](std::initializer_list<const char*> names) {
            std::vector<R> out;
            for (const char* n : names)
                out.push_back({n, T::Float, 1, {}});
            return out;
        };
        auto parser = [](const char* name, std::vector<R> records) {
            return R{name, T::Parser, 1, std::move(records)};
        };
        // vehAero::FileIO, vehEngine::FileIO, vehTransmission::FileIO,
        // vehDrivetrain::FileIO, vehWheel::FileIO, vehAxle::FileIO.
        std::vector<R> aero = {{"AngCDamp", T::Vec3, 1, {}},
                               {"AngVelDamp", T::Vec3, 1, {}},
                               {"AngVel2Damp", T::Vec3, 1, {}},
                               {"Drag", T::Float, 1, {}},
                               {"Down", T::Float, 1, {}}};
        std::vector<R> trans = {{"ManualNumGears", T::Int, 1, {}}, {"AutoNumGears", T::Int, 1, {}}};
        for (auto& r : floats({"Reverse", "Low", "High", "GearBias", "UpshiftBias", "DownshiftBiasMin",
                               "DownshiftBiasMax", "GearChangeTime"}))
            trans.push_back(std::move(r));
        const auto drivetrain = floats({"AngInertia", "BrakeDynamicCoef", "BrakeStaticCoef"});
        const auto wheel = floats({"SuspensionExtent", "SuspensionLimit", "SuspensionFactor", "SuspensionDampCoef",
                                   "SteeringLimit", "SteeringOffset", "BrakeCoef", "HandbrakeCoef", "CamberLimit",
                                   "WobbleLimit", "TireDispLimitLong", "TireDampCoefLong", "TireDragCoefLong",
                                   "TireDispLimitLat", "TireDampCoefLat", "TireDragCoefLat", "OptimumSlipPercent",
                                   "StaticFric", "SlidingFric"});
        const auto axle = floats({"TorqueCoef", "DampCoef"});
        // vehCarSim::FileIO, in registration order.
        return data::DatSchema{
            {"Mass", T::Float, 1, {}},
            {"InertiaBox", T::Vec3, 1, {}},
            {"CenterOfGravity", T::Vec3, 1, {}},
            {"BoundFriction", T::Float, 1, {}},
            {"BoundElasticity", T::Float, 1, {}},
            {"DrivetrainType", T::Int, 1, {}},
            {"SSSValue", T::Float, 1, {}},
            {"SSSThreshold", T::Float, 1, {}},
            {"CarFrictionHandling", T::Float, 1, {}},
            parser("Aero", aero),
            parser("Engine", floats({"AngInertia", "MaxHorsePower", "IdleRPM", "OptRPM", "MaxRPM", "GCL"})),
            parser("Trans", trans),
            parser("Drivetrain", drivetrain),
            parser("Freetrain", drivetrain),
            parser("WheelFront", wheel),
            parser("WheelBack", wheel),
            parser("AxleFront", axle),
            parser("AxleBack", axle),
        };
    }();
    return schema;
}

bool loadCarSimParams(const DatNode& b, CarSimParams& p, std::vector<std::string>* ignored) {
    if (b.name != "vehCarSim")
        return false;
    b.read("Mass", p.mass);
    b.read("InertiaBox", p.inertiaBox);
    b.read("CenterOfGravity", p.centerOfGravity);
    b.read("BoundFriction", p.boundFriction);
    b.read("BoundElasticity", p.boundElasticity);
    b.read("DrivetrainType", p.drivetrainType);
    b.read("SSSValue", p.sssValue);
    b.read("SSSThreshold", p.sssThreshold);
    b.read("CarFrictionHandling", p.carFrictionHandling);
    if (const auto* c = b.child("Aero"))
        loadAero(*c, p.aero);
    if (const auto* c = b.child("Engine"))
        loadEngine(*c, p.engine);
    if (const auto* c = b.child("Trans")) {
        loadTrans(*c, p.trans);
        noteIgnored(*c,
                    {"ManualNumGears", "AutoNumGears", "Reverse", "Low", "High", "GearBias", "UpshiftBias",
                     "DownshiftBiasMin", "DownshiftBiasMax", "GearChangeTime"},
                    "Trans.", ignored);
    }
    if (const auto* c = b.child("Drivetrain"))
        loadDrivetrain(*c, p.drivetrain);
    if (const auto* c = b.child("Freetrain"))
        loadDrivetrain(*c, p.freetrain);
    if (const auto* c = b.child("WheelFront"))
        loadWheelParams(*c, p.wheelFront);
    if (const auto* c = b.child("WheelBack"))
        loadWheelParams(*c, p.wheelBack);
    if (const auto* c = b.child("AxleFront"))
        loadAxle(*c, p.axleFront);
    if (const auto* c = b.child("AxleBack"))
        loadAxle(*c, p.axleBack);
    p.drivetrainType = std::clamp(p.drivetrainType, 0, 2);
    noteIgnored(b,
                {"Mass", "InertiaBox", "CenterOfGravity", "BoundFriction", "BoundElasticity",
                 "DrivetrainType", "SSSValue", "SSSThreshold", "CarFrictionHandling", "Aero", "Engine",
                 "Trans", "Drivetrain", "Freetrain", "WheelFront", "WheelBack", "AxleFront", "AxleBack"},
                "", ignored);
    return true;
}

bool loadGyroParams(const DatNode& b, GyroParams& g) {
    if (b.name != "vehGyro")
        return false;
    b.read("Drift", g.drift);
    b.read("Spin180", g.spin180);
    b.read("Reverse180", g.reverse180);
    b.read("Pitch", g.pitch);
    b.read("Roll", g.roll);
    return true;
}

bool loadStuckParams(const DatNode& b, StuckParams& s) {
    if (b.name != "vehStuck")
        return false;
    b.read("Turn", s.turn);
    b.read("Rotation", s.rotation);
    b.read("Translation", s.translation);
    b.read("TimeThresh", s.timeThresh);
    b.read("PosThresh", s.posThresh);
    b.read("MoveThresh", s.moveThresh);
    return true;
}

bool loadCarDamageParams(const DatNode& b, CarDamageParams& d) {
    if (b.name != "vehCarDamage")
        return false;
    b.read("MaxDamage", d.maxDamage);
    b.read("MedDamage", d.medDamage);
    b.read("ImpactThreshold", d.impactThreshold);
    b.read("RegenerateRate", d.regenerateRate);
    b.read("SmokeOffset", d.smokeOffset);
    b.read("SmokeOffset2", d.smokeOffset2);
    b.read("TextelDamageRadius", d.textelDamageRadius);
    int dp = 0;
    if (b.read("DoublePivot", dp))
        d.doublePivot = dp != 0;
    if (b.read("MirrorPivot", dp))
        d.mirrorPivot = dp != 0;
    return true;
}

bool loadTrailerParams(const DatNode& b, TrailerParams& t) {
    if (b.name != "vehTrailer")
        return false;
    b.read("Mass", t.mass);
    b.read("InertiaBox", t.inertiaBox);
    t.carHitchOffset = b.getVec3("CarHitchOffset");
    t.trailerHitchOffset = b.getVec3("TrailerHitchOffset");
    if (const auto* c = b.child("WheelFront"))
        loadWheelParams(*c, t.wheelFront);
    if (const auto* c = b.child("WheelBack"))
        loadWheelParams(*c, t.wheelBack);
    if (const auto* c = b.child("Drivetrain"))
        loadDrivetrain(*c, t.drivetrain);
    return true;
}

bool loadTrailerJointParams(const DatNode& b, TrailerJointParams& j) {
    // dgTrailerJoint::FileIO reads only these fields (no Offset0/Offset1,
    // no roll limits).
    if (b.name != "dgTrailerJoint")
        return false;
    b.read("ForceLimit", j.forceLimit);
    b.read("JointStatus", j.jointStatus);
    b.read("RestoreForceLean", j.restoreForceLean);
    b.read("DampConstLean", j.dampConstLean);
    b.read("DampLinearLean", j.dampLinearLean);
    b.read("RestoreForceRoll", j.restoreForceRoll);
    b.read("DampConstRoll", j.dampConstRoll);
    b.read("DampLinearRoll", j.dampLinearRoll);
    b.read("LeanLimit", j.leanLimit);
    b.read("LimitElasticityLean", j.limitElasticityLean);
    b.read("LimitElasticityRoll", j.limitElasticityRoll);
    b.read("FreeRange", j.freeRange);
    b.read("FreeLean", j.freeLean);
    b.read("FreeRoll", j.freeRoll);
    return true;
}

} // namespace mm2::phys
