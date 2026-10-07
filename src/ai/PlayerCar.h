#pragma once

#include "core/Math.h"

#include <cmath>

namespace mm2::ai {

// The player car as traffic and pedestrians see it (aiVehiclePlayer).
struct PlayerCar {
    Mat34 transform; // position = the car's centre point; faces -Z
    Vec3 velocity;
    float radius = 2.5f;  // the model's bounding radius (lvlInstance::GetRadius)
    float width = 2.0f;   // collision box size (vehCarSim)
    float length = 4.5f;
    float steering = 0.0f; // steering input, -1..1
    bool reversing = false; // in reverse gear
    bool horn = false;      // the player is sounding the horn
    bool valid = true;

    float speed() const { return std::abs(velocity.dot(transform.m2)); }
    // A player of default size at `pos` heading along `vel` (tools, tests).
    static PlayerCar at(const Vec3& pos, const Vec3& vel);
};

} // namespace mm2::ai
