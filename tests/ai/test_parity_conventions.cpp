// Round 3 of the parity audit, conventions: frames built from vectors face
// the way the vectors point (Angel convention: objects face -Z). See
// docs/parity/round3/conventions.md.

#include "ai/Traffic.h"

#include <gtest/gtest.h>

using namespace mm2;

// The stand-in player car of mm2tool's aisim and the tests faces along its
// velocity: its forward axis -m2 is the velocity's direction.
TEST(ConventionsParity, PlayerCarFacesItsVelocity) {
    for (const Vec3 v : {Vec3{0, 0, -10}, Vec3{10, 0, 0}, Vec3{-3, 0, 4}}) {
        const ai::PlayerCar p = ai::PlayerCar::at({1, 2, 3}, v);
        const Vec3 forward = -p.transform.m2;
        const Vec3 dir = v * (1.0f / v.mag());
        EXPECT_NEAR(forward.x, dir.x, 1e-5f);
        EXPECT_NEAR(forward.z, dir.z, 1e-5f);
        EXPECT_EQ(p.transform.m3, (Vec3{1, 2, 3}));
    }
    // Still: facing -Z.
    EXPECT_NEAR(ai::PlayerCar::at({}, {}).transform.m2.z, 1.0f, 1e-6f);
}
