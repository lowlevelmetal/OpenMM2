#pragma once

#include "phys/World.h"
#include "phys/vehicle/Aero.h"
#include "phys/vehicle/Drivetrain.h"
#include "phys/vehicle/Engine.h"
#include "phys/vehicle/Gyro.h"
#include "phys/vehicle/Stuck.h"
#include "phys/vehicle/Transmission.h"
#include "phys/vehicle/TuneParams.h"
#include "phys/vehicle/VehicleGeometry.h"
#include "phys/vehicle/Wheel.h"

#include <array>
#include <functional>

namespace mm2::phys {

// Damage bookkeeping (vehCarDamage). CurrentDamage falls by RegenerateRate
// per second; impacts stronger than ImpactThreshold add to it while the car
// moves at 10 mph or more, or when the other party is a vehicle
// (vehCarDamage::ApplyImpact). The impact value is the impulse scaled by the
// other body's share of the two masses (vehCarDamage::InsertImpact; mapping
// OpenMM2's contact impulse onto MM2's impact data is inferred). Damage is
// the 0..1 fraction between MedDamage and MaxDamage; the car is wrecked at
// MaxDamage.
struct CarDamage {
    CarDamageParams params;
    float currentDamage = 0.0f;
    float damage = 0.0f;      // 0..1
    float globalScale = 1.0f; // ?GlobalDamageScale@@3MA
    bool enabled = true;

    float maxScaled() const { return params.maxDamage * globalScale; }
    float medScaled() const { return params.medDamage * globalScale; }
    void reset() {
        currentDamage = 0.0f;
        damage = 0.0f;
    }
    // `value`: impulse * other mass share; `speedMph`: the car's speed.
    void impact(float value, float speedMph, bool otherIsVehicle);
    void update(float dt);
    bool wrecked() const { return enabled && maxScaled() <= currentDamage; }
};

// vehAxle: anti-roll coupling between an axle's wheels (TorqueCoef,
// DampCoef) and the axle's visual roll.
struct Axle {
    AxleParams params;
    float stiffness = 0.0f; // TorqueCoef * Izz
    float damping = 0.0f;   // 2 sqrt(stiffness * Izz) * DampCoef
    float roll = 0.0f;      // visual roll of the wheels (rad)
    Mat34 matrix;           // pivot (model space)
    float rollFactor = 1.0f; // 1 / lateral offset of the left wheel (1 without a pivot)
};

struct CarSimOptions {
    // ?WeatherFriction@@3MA: 1, or 0.8 in snow (0.75 at night) in MM2
    // (mmGame::InitWeather).
    float weatherFriction = 1.0f;
    // Damage can wreck the car (which then brakes and stops responding).
    bool damage = true;
    // vehGyro and vehAxle coupling (the original always runs them; kept
    // switchable for comparisons).
    bool gyro = true;
    bool axleCoupling = true;
    // The player's car: mmPlayer::Update's input overrides apply (handbrake
    // below 4 mph without throttle, wrecked and finished states).
    bool player = false;
};

// vehCarSim (Midtown Madness 2): a car, verified against the build 3393
// code. Construct, init(), add body() to a World, set inputs each frame.
//
// Coordinate conventions: car model space has its origin near the ground
// under the body centre and faces -Z. The model's origin is the rigid body's
// position plus R * CenterOfGravity (vehCarSim::SetWorldMatrix), so the
// centre of mass sits at -CenterOfGravity in model space. Steering +1 turns
// right.
class CarSim final : public BodyController {
public:
    using Options = CarSimOptions;

    CarSim() = default;
    CarSim(const CarSim&) = delete;
    CarSim& operator=(const CarSim&) = delete;

    // vehCarSim::Init + ConfigureDrivetrain.
    void init(const CarSimParams& params, const VehicleGeometry& geometry, const Options& options = {});
    void setGyroParams(const GyroParams& p) { gyro.configure(p); }
    void setStuckParams(const StuckParams& p) { stuck.configure(p); }
    void setDamageParams(const CarDamageParams& p) { damage.params = p; }

    // Places the car's model origin at `model` and resets all state
    // (vehCarSim::Reset).
    void reset(const Mat34& model);

    // Inputs (vehCarSim brake/handbrake/steering, vehEngine throttle).
    void setInputs(float throttle, float brakes, float steering, float handBrake);

    // World matrix of the car's model origin (vehCarSim world matrix).
    Mat34 modelMatrix() const;
    // World matrix of a wheel (steered, displaced, spun, rolled with its axle).
    Mat34 wheelMatrix(int i) const;

    // vehCarSim::OnGround: number of wheels touching the ground.
    int wheelsOnGround() const;
    bool onGround() const { return wheelsOnGround() > 0; }
    // vehCarSim::GetSSSFactor.
    float sssFactor(float speed) const;
    float speed() const { return m_speed; }       // m/s, |velocity . car Z axis|
    float speedMph() const { return m_speedMph; } // m_speed * MetricFactor (mph)

    // Ground used by the wheels; defaults to the World the body is in.
    void setGround(const GroundQuery* ground) { m_ground = ground; }
    // Called with every impact report (audio, game logic).
    std::function<void(const Impact&)> onImpactCallback;

    // BodyController.
    void beforeIntegrate(Body& body, float dt, const World& world) override;
    void afterIntegrate(Body& body, float dt, const World& world) override;
    void onImpact(Body& body, const Impact& impact) override;

    Body body;
    CarSimParams params;
    Options options;
    Vec3 centerOfGravity; // vehCarSim CenterOfGravity
    Engine engine;
    Transmission trans;
    // 0, 1: Freetrain (front or back wheels, one each); 2: Drivetrain
    // (engine-driven). Updated in that order, as vehCarSim's children.
    std::array<Drivetrain, 3> drivetrains;
    std::array<Wheel, 4> wheels; // FL, FR, BL, BR
    std::array<Axle, 2> axles;   // front, back
    Aero aero;
    Gyro gyro;
    Stuck stuck;
    CarDamage damage;

    float brakes = 0.0f;
    float handBrake = 0.0f;
    float steering = 0.0f;
    // mmPlayer +0x2258: the player has finished the race (brakes on, wheel
    // turned full left from then on). Set by the game.
    bool raceFinished = false;

private:
    WheelEnv makeEnv(float dt, const World& world);
    void updateAxles();
    Drivetrain& primary() { return drivetrains[2]; }

    std::array<int, 3> m_drivetrainOrder{0, 1, 2};
    int m_numDrivetrains = 3;
    const GroundQuery* m_ground = nullptr;
    float m_speed = 0.0f;
    float m_speedMph = 0.0f;
};

} // namespace mm2::phys
