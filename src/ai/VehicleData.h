#pragma once

#include "core/Math.h"
#include "vfs/Vfs.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace mm2::ai {

// tune/vehicle/<model>.aivehicledata: physical description of an ambient
// vehicle type (aiVehicleData). Size is the full extent of the collision box
// (x width, y height, z length); CG is relative to the model origin.
struct VehicleData {
    std::string model; // e.g. "va_taxi_f"
    float mass = 1000.0f;
    Vec3 size{2.0f, 1.5f, 4.5f};
    Vec3 maxAng;
    float elasticity = 0.5f;
    float friction = 0.5f;
    float maxDamage = 0.0f;
    float ptxThresh = 0.0f;
    float spring = 0.0f;
    float damping = 0.0f;
    float limit = 0.0f;
    float rubberSpring = 0.0f;
    float rubberDamp = 0.0f;
    Vec3 cg;
    // aiVehicleManager::AddVehicleDataEntry: wheel pivots from
    // geometry/<model>_whlN.mtx and the wheel radius from WHL0's box.
    std::array<Vec3, 6> wheels{};
    int wheelCount = 0; // pivots found, in order WHL0..
    float wheelRadius = 0.0f;

    float length() const { return size.z; }
    float width() const { return size.x; }
};

// Loads tune/vehicle/<model>.aivehicledata. Missing files return defaults
// sized like a family car (logged).
std::optional<VehicleData> loadVehicleData(const vfs::Vfs& vfs, std::string_view model,
                                           std::string* error = nullptr);

} // namespace mm2::ai
