// Parity checks for the phys-core audit (docs/parity/phys-core.md): the
// rigid body, sleep, colliders, the world's sampling and the AGE math
// helpers, against the behaviour of midtown2.exe build 3393.

#include "phys/AgeMath.h"
#include "phys/Collider.h"
#include "phys/InertialCS.h"
#include "phys/Sleep.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace mm2;
using namespace mm2::phys;

TEST(ParityPhysCore, InertialCSDefaultsAreMM2s) {
    // phInertialCS::phInertialCS: Init(1, 1, 1, 1), speed limit 500, angular
    // velocity limit 5 rad/s per body axis.
    const InertialCS ics;
    EXPECT_EQ(ics.mass, 1.0f);
    EXPECT_EQ(ics.inertia.x, 1.0f);
    EXPECT_EQ(ics.maxSpeed, 500.0f);
    EXPECT_EQ(ics.maxAngVelocity.x, 5.0f);
    EXPECT_EQ(ics.maxAngVelocity.z, 5.0f);
    EXPECT_EQ(ics.gravity.y, -kGravity);
}

TEST(ParityPhysCore, SpinIsLimitedPerBodyAxisByDefault) {
    // phInertialCS::Update limits every body's spin, not just the cars'
    // (props and traffic that left their rails keep the constructor's 5).
    InertialCS ics;
    ics.setMass(1.0f, 1.0f, 1.0f, 10.0f);
    ics.gravity = {};
    ics.angularMomentum = {0.0f, 1000.0f, 0.0f};
    ics.update(1.0f / 60.0f, 60.0f);
    EXPECT_FLOAT_EQ(ics.angularVelocity.y, 5.0f);
    EXPECT_FLOAT_EQ(ics.angularMomentum.y, 5.0f * ics.inertia.y);
}

TEST(ParityPhysCore, ImplicitContactCouplesForceIntoTurn) {
    // A downward force at the CG compresses a contact at +x; its stiffness
    // must turn the body so that the +x side lifts (positive spin about z).
    // phInertialCS::Update scales the angular right-hand side by -(h/m).
    InertialCS ics;
    ics.setMass(2.0f, 1.0f, 4.0f, 1000.0f);
    ics.gravity = {};
    const float h = 1.0f / 60.0f;
    const Mat34 k{{}, {0.0f, 5.0e5f, 0.0f}, {}, {}};
    ics.applyContactForce({}, {1.0f, 0.0f, 0.0f}, k);
    ics.applyForce({0.0f, -1.0e5f, 0.0f});
    ics.update(h, 1.0f / h);
    EXPECT_GT(ics.angularVelocity.z, 0.0f);
    // MM2's linear update then adds h dw XK (the sign opposite to the one
    // its angular solve assumes), which with a contact this stiff lifts the
    // body: 0.836 m/s with these numbers.
    EXPECT_NEAR(ics.linearVelocity.y, 0.836f, 0.01f);
}

TEST(ParityPhysCore, SleepingBodyKeepsOnlyThisSamplesPushes) {
    // phInertialCS::Update keeps the push bookkeeping of an inactive body:
    // the pushes MoveICS applied become lastPush and the frame push starts
    // again from zero each sample.
    InertialCS ics;
    ics.gravity = {};
    Sleep sleep;
    sleep.init(&ics);
    for (int i = 0; i < 20 && sleep.state == Sleep::Awake; ++i) {
        sleep.update(60.0f);
        ics.update(1.0f / 60.0f, 60.0f);
    }
    ASSERT_EQ(ics.state, InertialCS::Asleep);
    for (int i = 0; i < 3; ++i) {
        ics.applyPush({0.0f, 0.01f, 0.0f});
        ics.moveICS();
        ics.update(1.0f / 60.0f, 60.0f);
        EXPECT_FLOAT_EQ(ics.lastPush.y, 0.01f);
        EXPECT_EQ(ics.framePush.y, 0.0f);
    }
    EXPECT_NEAR(ics.matrix.m3.y, 0.03f, 1e-6f);
}

TEST(ParityPhysCore, SendToSleepFreezesPushes) {
    // phSleep::SendToSleep calls phInertialCS::Freeze, which clears the
    // pending pushes as well as the motion.
    InertialCS ics;
    Sleep sleep;
    sleep.init(&ics);
    sleep.sleepAfter = 1;
    ics.linearPush = {0.0f, 0.0f, 0.0f};
    ics.turnForce = {0.1f, 0.0f, 0.0f};
    sleep.update(60.0f);
    ASSERT_EQ(sleep.state, Sleep::Asleep);
    EXPECT_EQ(ics.turnForce.x, 0.0f);
    EXPECT_EQ(ics.linearVelocity.mag2(), 0.0f);
}

TEST(ParityPhysCore, CMFilteredVelocityAddsTheLastPush) {
    // phInertialCS::GetCMFilteredVelocity: velocity + lastPush * InvSeconds.
    InertialCS ics;
    ics.linearVelocity = {1.0f, 2.0f, 3.0f};
    ics.lastPush = {0.1f, 0.0f, -0.2f};
    const Vec3 v = ics.cmFilteredVelocity(60.0f);
    EXPECT_FLOAT_EQ(v.x, 7.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, -9.0f);
}

TEST(ParityPhysCore, StaticCollidersCountAsNotMoving) {
    // phCollider::Init(bound, matrix): maxMoved 0, barely moved.
    const Mat34 m;
    Collider c;
    c.initStatic(nullptr, &m);
    EXPECT_TRUE(c.barelyMoved);
    EXPECT_EQ(c.maxMoved, 0.0f);
    Collider d;
    InertialCS ics;
    d.init(nullptr, &ics.matrix, &ics);
    EXPECT_FALSE(d.barelyMoved);
}

TEST(ParityPhysCore, OversamplingSkipsFramesUnderAMillisecond) {
    // dgPhysManager::Update: ceil((frame - 0.001) / step) samples, so a
    // frame under a millisecond runs none.
    World world;
    EXPECT_EQ(world.advanceOversampled(0.0005f), 0);
    EXPECT_EQ(world.advanceOversampled(0.0f), 0);
    EXPECT_EQ(world.advanceOversampled(0.002f), 1);
}

TEST(ParityPhysCore, SolveSVDFullRankSolvesRowVectorSystem) {
    // x * M = b for a well-conditioned M.
    const Mat34 m{{4.0f, 1.0f, 0.5f}, {1.0f, 3.0f, 0.2f}, {0.5f, 0.2f, 2.0f}, {}};
    const Vec3 x{0.3f, -1.2f, 2.5f};
    const Vec3 b = m.transformDir(x);
    const Vec3 r = age::solveSVD(m, b);
    EXPECT_NEAR(r.x, x.x, 1e-5f);
    EXPECT_NEAR(r.y, x.y, 1e-5f);
    EXPECT_NEAR(r.z, x.z, 1e-5f);
    EXPECT_EQ(age::solveSVD(Mat34{{}, {}, {}, {}}, b).mag2(), 0.0f);
}

TEST(ParityPhysCore, SolveSVDRankFallbacks) {
    // Rank 1: b projected on the row of the largest element.
    const Vec3 r1 = age::solveSVD(Mat34{{2.0f, 0.0f, 0.0f}, {}, {}, {}}, {4.0f, 1.0f, 1.0f});
    EXPECT_FLOAT_EQ(r1.x, 2.0f);
    EXPECT_EQ(r1.y, 0.0f);
    EXPECT_EQ(r1.z, 0.0f);
    // Rank 2: the minimum-norm solution in the plane the matrix spans.
    const Vec3 r2 = age::solveSVD(Mat34{{2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}, {}, {}}, {4.0f, 9.0f, 5.0f});
    EXPECT_FLOAT_EQ(r2.x, 2.0f);
    EXPECT_FLOAT_EQ(r2.y, 3.0f);
    EXPECT_EQ(r2.z, 0.0f);
}

TEST(ParityPhysCore, MakeRotateAxisShortcuts) {
    // Matrix34::MakeRotate: axis-aligned axes take MakeRotateX/Y/Z with the
    // axis' sign; a zero axis takes the -z branch.
    const Mat34 negZ = age::makeRotate({0.0f, 0.0f, -1.0f}, 0.4f);
    const Mat34 zero = age::makeRotate({0.0f, 0.0f, 0.0f}, 0.4f);
    const Mat34 rz = Mat34::rotationZ(-0.4f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(negZ.row(i), rz.row(i));
        EXPECT_EQ(zero.row(i), rz.row(i));
    }
    // A general axis is normalised first.
    const Mat34 a = age::makeRotate({0.0f, 2.0f, 2.0f}, 0.7f);
    const Mat34 b = age::makeRotateUnitAxis(Vec3{0.0f, 1.0f, 1.0f}.normalized(), 0.7f);
    for (int i = 0; i < 3; ++i)
        EXPECT_LT(a.row(i).dist(b.row(i)), 1e-6f);
}

TEST(ParityPhysCore, NormalizeKeepsTheBackAxis) {
    // Matrix34::Normalize keeps m2's direction: m0 = |m1 x m2|, m1 = |m2 x m0|.
    Mat34 m;
    m.m0 = {1.0f, 0.1f, 0.0f};
    m.m1 = {0.05f, 1.0f, 0.1f};
    m.m2 = {0.0f, 0.0f, 2.0f};
    m.normalize();
    EXPECT_FLOAT_EQ(m.m2.z, 1.0f);
    EXPECT_NEAR(m.m0.dot(m.m1), 0.0f, 1e-6f);
    EXPECT_NEAR(m.m0.dot(m.m2), 0.0f, 1e-6f);
    EXPECT_NEAR(m.m1.mag(), 1.0f, 1e-6f);
}

namespace {

// Three rooms along x (1: x < 100, 2: 100..200, 3: beyond), each next to
// the following one.
class StripLevel final : public Level {
public:
    int findRoom(const Vec3& p, int) const override { return p.x < 100.0f ? 1 : (p.x < 200.0f ? 2 : 3); }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    int neighbors(int* out, int max, int room) const override {
        int n = 0;
        if (room > 1 && n < max)
            out[n++] = room - 1;
        if (room < 3 && n < max)
            out[n++] = room + 1;
        return n;
    }
    void collect(const int*, int, const Vec3&, float, LevelBound& out) const override { out.clear(); }
    void instances(int, std::vector<Instance*>&) const override {}
};

struct DetachCountingBody final : Body {
    int detached = 0;
    void detach() override { ++detached; }
};

} // namespace

TEST(ParityPhysCore, TypeOneMoversOutsideTheActiveRoomsAreDetached) {
    // dgPhysManager::DeclareMover / Update: the player (type 4) makes its
    // room and the neighbours active; a type-1 mover elsewhere is left out
    // of the frame and detached.
    StripLevel level;
    World world;
    world.setLevel(&level);
    DetachCountingBody player, near, far;
    player.place(Mat34::translation({0.0f, 10.0f, 0.0f}));
    near.place(Mat34::translation({150.0f, 10.0f, 0.0f}));
    far.place(Mat34::translation({250.0f, 10.0f, 0.0f}));
    player.declare(4, 0x1b);
    near.declare(1, 0x1b);
    far.declare(1, 0x1b);
    world.add(&player);
    world.add(&near);
    world.add(&far);
    world.advanceFixed(1.0f / 60.0f);
    EXPECT_TRUE(player.player);
    EXPECT_TRUE(world.isActive(&near));
    EXPECT_FALSE(world.isActive(&far));
    EXPECT_EQ(near.detached, 0);
    EXPECT_EQ(far.detached, 1);
    EXPECT_LT(near.ics.linearVelocity.y, 0.0f);
    EXPECT_EQ(far.ics.linearVelocity.y, 0.0f);
}

TEST(ParityPhysCore, MoverTableHoldsThirtyTwo) {
    World world;
    std::vector<std::unique_ptr<Body>> bodies;
    for (int i = 0; i < 33; ++i) {
        bodies.push_back(std::make_unique<Body>());
        world.add(bodies.back().get());
    }
    world.advanceFixed(1.0f / 60.0f);
    EXPECT_LT(bodies[31]->ics.linearVelocity.y, 0.0f);
    EXPECT_EQ(bodies[32]->ics.linearVelocity.y, 0.0f);
}

TEST(ParityPhysCore, UndeclaredOrNonUpdatingBodiesStandStill) {
    World world;
    Body a, b;
    world.add(&a);
    world.add(&b);
    b.declared = false;
    world.advanceFixed(1.0f / 60.0f);
    EXPECT_LT(a.ics.linearVelocity.y, 0.0f);
    EXPECT_EQ(b.ics.linearVelocity.y, 0.0f);
    // DeclareMover's flags: without 0x1 the body does not update.
    a.declare(2, 0x1a);
    EXPECT_FALSE(a.updates);
    EXPECT_TRUE(a.collideTerrain && a.collideInstances && a.collideMovers);
    const float v = a.ics.linearVelocity.y;
    world.advanceFixed(1.0f / 60.0f);
    EXPECT_EQ(a.ics.linearVelocity.y, v);
}
