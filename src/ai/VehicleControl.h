#pragma once

// Interface between AI drivers and physically simulated cars (phase 2:
// opponents and police, MM1 aiVehicleOpponent / aiVehiclePolice driving an
// mmCarSim through its inputs; also ambient cars after an impact). The AI
// reads the car's state and writes the same inputs a player would give, so
// opponents obey exactly the same physics as the player (as in the original,
// where opponents are full vehCarSim cars).
//
// The game owns the physics cars and implements ControlledVehicle on top of
// src/phys; the AI never touches physics directly.

#include "core/Math.h"

namespace mm2::ai {

// Driver inputs, in the ranges the player's controls produce.
struct VehicleControls {
    float steering = 0.0f;  // -1 full right .. +1 full left
    float throttle = 0.0f;  // 0 .. 1
    float brake = 0.0f;     // 0 .. 1
    float handbrake = 0.0f; // 0 .. 1
    bool reverse = false;   // select reverse (automatic transmission)
    bool horn = false;
    bool siren = false; // police
};

// What the AI may observe about a car each step.
struct VehicleState {
    Mat34 transform;            // body placement (Angel convention: faces -m2)
    Vec3 velocity;              // world, m/s
    Vec3 angularVelocity;       // world, rad/s
    float speed = 0.0f;         // signed, along the car's forward axis
    float steeringAngle = 0.0f; // current front wheel angle, radians
    float maxSteeringAngle = 0.5f;
    int gear = 0;
    bool onGround = true;
    float damage = 0.0f; // 0 .. 1
    bool stuck = false;  // vehStuck state
};

class ControlledVehicle {
public:
    virtual ~ControlledVehicle() = default;
    virtual VehicleState state() const = 0;
    virtual void setControls(const VehicleControls& controls) = 0;
    // Teleport (reset after getting stuck, aiStuck / vehStuck behaviour).
    virtual void reset(const Mat34& transform) = 0;
};

} // namespace mm2::ai
