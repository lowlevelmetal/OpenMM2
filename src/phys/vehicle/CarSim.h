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

// Damage bookkeeping (vehCarDamage). Accumulation is ported from MM1's
// mmCar::Impact: impacts closing faster than 4 m/s add their impulse to
// CurrentDamage. MM2's ImpactThreshold is applied as a minimum impulse
// (inferred). Damage is the 0..1 smoke/visual fraction between MedDamage
// and MaxDamage (mmCarSim::UpdateDamage).
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
    void impact(float impulse, float closingSpeed);
    void update(float dt);
    bool wrecked() const { return currentDamage > maxScaled(); }
};

// Visual axle (MM1 mmAxle::Update): centred between its wheels and rolled to
// follow them.
struct Axle {
    AxleParams params;
    Mat34 matrix;
};

struct CarSimOptions {
    // OpenMM2 approximations of MM2-only mechanisms; off by default
    // because their behaviour is not known (see docs/physics.md).
    bool gyro = false;
    bool axleCoupling = false;
    // MM1 *mmCarSim::Realism (game option); 1 = full simulation.
    float realism = 1.0f;
    // ?WeatherFriction@@3MA: 1, or 0.8 in snow (0.75 at night) in MM2.
    float weatherFriction = 1.0f;
    // Room flags & 3 (tunnels, indoors): weather friction ignored.
    bool indoors = false;
    // Damage disables the car and changes impact response.
    bool damage = true;
    // Drivetrain::mm1ExplicitSpin: MM1 build 1560's explicit wheel spin, for
    // comparison (unstable with stiff tyres at 60 Hz).
    bool mm1ExplicitSpin = false;
};

// vehCarSim: a player-style car, ported from MM1's mmCarSim (Open1560
// game.asm) with MM2's tune format. Construct, init(), add body() to a World,
// set inputs each frame.
//
// Coordinate conventions: car model space has its origin near the ground
// under the body centre and faces -Z. The rigid body is centred on
// CenterOfGravity (model space). Steering +1 turns right.
class CarSim final : public BodyController {
public:
    using Options = CarSimOptions;

    CarSim() = default;
    CarSim(const CarSim&) = delete;
    CarSim& operator=(const CarSim&) = delete;

    // Builds the car (mmCarSim::Init + ConfigureDrivetrain).
    void init(const CarSimParams& params, const VehicleGeometry& geometry, const Options& options = {});
    void setGyroParams(const GyroParams& p) { gyro.configure(p); }
    void setStuckParams(const StuckParams& p) { stuck.configure(p); }
    void setDamageParams(const CarDamageParams& p) { damage.params = p; }

    // Places the car so that its model origin is at `model` (the front-right
    // wheel resting on the origin's height, as mmCarSim::SetResetPos does),
    // and resets all state (mmCarSim::Reset).
    void reset(const Mat34& model);

    // Inputs (mmCarSim Brakes/HandBrake/Steering, vehEngine ThrottleInput).
    void setInputs(float throttle, float brakes, float steering, float handBrake);

    // World matrix of the car's model origin (for rendering).
    Mat34 modelMatrix() const;
    // World matrix of a wheel (model-space centre convention as the car).
    Mat34 wheelMatrix(int i) const;

    bool onGround() const;
    float speed() const { return m_speed; } // m/s (|FrameVelocity|)
    float speedMph() const { return m_speedMph; }

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
    Vec3 centerOfGravity; // model space
    Engine engine;
    Transmission trans;
    std::array<Drivetrain, 3> drivetrains; // DriveTrain1/2 free, DriveTrain3 engine-driven
    std::array<Wheel, 4> wheels;           // FL, FR, BL, BR
    std::array<Axle, 2> axles;
    Aero aero;
    Gyro gyro;
    Stuck stuck;
    CarDamage damage;

    float brakes = 0.0f;
    float handBrake = 0.0f;
    float steering = 0.0f;

private:
    WheelEnv makeEnv(float dt, const World& world) const;
    void updateAxles();
    void applyAxleCoupling();

    std::array<int, 3> m_drivetrainOrder{0, 1, 2};
    int m_numDrivetrains = 3;
    const GroundQuery* m_ground = nullptr;
    float m_speed = 0.0f;
    float m_speedMph = 0.0f;
};

} // namespace mm2::phys
