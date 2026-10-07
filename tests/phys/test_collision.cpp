// MM2's collision (phImpact, phBoundPolygonal, lvlSDL, dgPhysManager,
// vehCarDamage), checked against the original's rules.
#include "phys/TestLevel.h"
#include "phys/Bound.h"
#include "phys/Collider.h"
#include "phys/Collision.h"
#include "phys/Impact.h"
#include "phys/Joint.h"
#include "phys/World.h"
#include "phys/vehicle/CarSim.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace mm2;
using namespace mm2::phys;

namespace {

std::unique_ptr<BoundBox> ownBox(const Vec3& size, float elasticity, float friction) {
    auto b = std::make_unique<BoundBox>(size);
    b->makeOwnMaterial();
    b->setElasticity(elasticity);
    b->setFriction(friction);
    return b;
}

// A body of `mass` (unit inertia box) at `position` moving at `velocity`.
void placeBody(InertialCS& ics, float mass, const Vec3& position, const Vec3& velocity) {
    ics.setMass(1, 1, 1, mass);
    ics.matrix = Mat34::identity();
    ics.matrix.m3 = position;
    ics.linearVelocity = velocity;
    ics.linearMomentum = velocity * mass;
}

// A box body for World tests: the bound centred on the centre of mass.
struct BoxBody {
    Body body;
    std::unique_ptr<BoundBox> bound;
    BoxBody(const Vec3& size, float mass, const Vec3& position, float elasticity = 0.5f, float friction = 1.0f) {
        bound = ownBox(size, elasticity, friction);
        body.ics.setMass(size.x, size.y, size.z, mass);
        body.collisionBound = bound.get();
        Mat34 m = Mat34::identity();
        m.m3 = position;
        body.place(m);
    }
};

} // namespace

TEST(Impact, FrictionMultipliesElasticityIsCapped) {
    // phImpact::FindFrictionAndElasticity: friction fA * fB, elasticity
    // min(eA * eB, 1).
    auto a = ownBox({1, 1, 1}, 0.8f, 0.5f);
    auto b = ownBox({1, 1, 1}, 0.9f, 0.8f);
    Mat34 m = Mat34::identity();
    Collider ca, cb;
    ca.init(a.get(), &m, nullptr);
    cb.init(b.get(), &m, nullptr);
    Impact im;
    im.colliderA = &ca;
    im.colliderB = &cb;
    im.findFrictionAndElasticity();
    EXPECT_FLOAT_EQ(im.friction, 0.4f);
    EXPECT_FLOAT_EQ(im.elasticity, 0.72f);
    a->setElasticity(1.5f);
    b->setElasticity(1.2f);
    im.findFrictionAndElasticity();
    EXPECT_FLOAT_EQ(im.elasticity, kElasticityCap);
}

TEST(Impact, HeadOnImpulseStopsAndBounces) {
    // phImpact::CalcCollision against something immovable: the impulse that
    // stops the contact point, times 1 + elasticity, scaled by the weight;
    // B (no InertialCS) takes nothing.
    auto boxA = ownBox({1, 1, 1}, 0.5f, 1.0f);
    auto boxB = ownBox({1, 1, 1}, 0.4f, 1.0f);
    InertialCS ics;
    placeBody(ics, 100.0f, {0, 1, 0}, {0, -5, 0});
    Mat34 ma = ics.matrix, mb = Mat34::identity();
    Collider ca, cb;
    ca.init(boxA.get(), &ma, &ics);
    cb.init(boxB.get(), &mb, nullptr);
    Impact im;
    im.colliderA = &ca;
    im.colliderB = &cb;
    im.position = ics.matrix.m3;
    im.normal = {0, 1, 0};
    calcImpact(im, 1.0f);
    EXPECT_NEAR(ics.linearImpulse.y, 1.2f * 100.0f * 5.0f, 1e-2f);
    EXPECT_NEAR(ics.linearImpulse.x, 0.0f, 1e-4f);
    // Weighted (dgPhysManager: 1 / the pair's impact count).
    ics.linearImpulse = {};
    calcImpact(im, 0.25f);
    EXPECT_NEAR(ics.linearImpulse.y, 0.25f * 600.0f, 1e-2f);
}

TEST(Impact, SeparatingBodiesGetNoImpulse) {
    auto box = ownBox({1, 1, 1}, 0.5f, 1.0f);
    InertialCS ics;
    placeBody(ics, 100.0f, {0, 1, 0}, {0, 0.02f, 0});
    Mat34 ma = ics.matrix, mb = Mat34::identity();
    Collider ca, cb;
    ca.init(box.get(), &ma, &ics);
    cb.init(box.get(), &mb, nullptr);
    Impact im;
    im.colliderA = &ca;
    im.colliderB = &cb;
    im.position = ics.matrix.m3;
    im.normal = {0, 1, 0};
    calcImpact(im, 1.0f);
    // Separating faster than 0.01 m/s: no impulse.
    EXPECT_EQ(ics.linearImpulse.y, 0.0f);
}

TEST(Impact, FrictionConeRedirectsTheImpulse) {
    // Sliding at 3 m/s while closing at 1 m/s with friction 0.5: the
    // stopping impulse's tangential part exceeds 0.5 of its normal part, so
    // the impulse runs along normal + 0.5 * the tangential direction, sized
    // to stop the normal motion.
    auto a = ownBox({1, 1, 1}, 0.0f, 1.0f);
    auto b = ownBox({1, 1, 1}, 0.0f, 0.5f);
    InertialCS ics;
    placeBody(ics, 100.0f, {0, 1, 0}, {3, -1, 0});
    Mat34 ma = ics.matrix, mb = Mat34::identity();
    Collider ca, cb;
    ca.init(a.get(), &ma, &ics);
    cb.init(b.get(), &mb, nullptr);
    Impact im;
    im.colliderA = &ca;
    im.colliderB = &cb;
    im.position = ics.matrix.m3;
    im.normal = {0, 1, 0};
    calcImpact(im, 1.0f);
    EXPECT_NEAR(ics.linearImpulse.y, 100.0f, 1e-2f);
    EXPECT_NEAR(ics.linearImpulse.x, -50.0f, 1e-2f);
}

TEST(Impact, PushesShareTheDepthAndNeverPushDown) {
    // Two equal bodies, A on top: each would take half the depth, but B's
    // half points down, so A takes it all.
    auto box = ownBox({1, 1, 1}, 0.5f, 1.0f);
    InertialCS ia, ib;
    placeBody(ia, 100.0f, {0, 1, 0}, {});
    placeBody(ib, 100.0f, {0, 0, 0}, {});
    Mat34 ma = ia.matrix, mb = ib.matrix;
    Collider ca, cb;
    ca.init(box.get(), &ma, &ia);
    cb.init(box.get(), &mb, &ib);
    Impact im;
    im.colliderA = &ca;
    im.colliderB = &cb;
    im.position = {0, 0.5f, 0};
    im.normal = {0, 1, 0};
    im.depth = 0.1f;
    calcImpact(im, 1.0f);
    EXPECT_NEAR(ia.linearPush.y, 0.1f, 1e-5f);
    EXPECT_NEAR(ib.linearPush.y, 0.0f, 1e-6f);
    // Against something immovable, the body takes the whole depth.
    InertialCS ic;
    placeBody(ic, 100.0f, {0, 1, 0}, {});
    Mat34 mc = ic.matrix;
    Collider cc, cs;
    cc.init(box.get(), &mc, &ic);
    cs.init(box.get(), &mb, nullptr);
    Impact im2 = im;
    im2.colliderA = &cc;
    im2.colliderB = &cs;
    calcImpact(im2, 1.0f);
    EXPECT_NEAR(ic.linearPush.y, 0.1f, 1e-5f);
}

TEST(Collision, BoxesFaceToFaceMakeImpacts) {
    // phBoundBox::FindImpactsBoxToBox: a box sunk 5 cm into another one
    // below it, both at rest: contacts on the shared face, normal from B
    // towards A.
    auto a = ownBox({1, 1, 1}, 0.5f, 1.0f);
    auto b = ownBox({4, 1, 4}, 0.5f, 1.0f);
    InertialCS ia, ib;
    placeBody(ia, 100.0f, {0, 0.95f, 0}, {});
    placeBody(ib, 100.0f, {0, 0, 0}, {});
    Mat34 ma = ia.matrix, mb = ib.matrix;
    Collider ca, cb;
    ca.init(a.get(), &ma, &ia);
    cb.init(b.get(), &mb, &ib);
    std::vector<Intersection> isa(kMaxIntersections), isb(kMaxIntersections);
    std::vector<Impact> impacts(kMaxImpacts);
    const int n = testBoundGeneric(*a, ca, *b, cb, isa.data(), isb.data(), impacts.data(), kMaxIntersections,
                                   kMaxImpacts, mb.m3 - ma.m3);
    ASSERT_GT(n, 0);
    for (int i = 0; i < n; ++i) {
        const Impact& im = impacts[static_cast<std::size_t>(i)];
        const float sign = im.colliderA == &ca ? 1.0f : -1.0f;
        EXPECT_NEAR(im.normal.y * sign, 1.0f, 1e-4f);
        EXPECT_NEAR(im.depth, 0.05f, 1e-4f);
    }
}

TEST(Level, ImpactsTakeTheLevelMaterialAndPushTheBodyUp) {
    // lvlSDL::CollidePolyToLevel + its impact search: a box sunk into a
    // floor of material 1. The level is side A; the normal points from the
    // box into the floor; the level's component is the polygon's material.
    auto box = ownBox({1, 1, 1}, 0.5f, 1.0f);
    LevelBound level;
    level.clear();
    const Vec3 corners[4] = {{-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10}};
    level.addPolygon(corners, 4, {0, 1, 0}, 1);
    InertialCS ics;
    placeBody(ics, 100.0f, {0, 0.45f, 0}, {0, -2, 0});
    Mat34 m = ics.matrix;
    Mat34 last = m;
    last.m3.y += 2.0f / 60.0f;
    Collider body, levelCollider;
    Mat34 identity = Mat34::identity();
    body.init(box.get(), &m, &ics);
    levelCollider.init(&level, &identity, nullptr);
    sampleTime() = {1.0f / 60.0f, 60.0f};
    std::vector<Intersection> isects(kMaxIntersections);
    std::vector<Impact> impacts(kMaxImpacts);
    int found = 0;
    ASSERT_GT(collidePolyToLevel(level, *box, nullptr, m, last, isects.data(), kMaxIntersections, found, true), 0);
    const int n = findLevelImpacts(level, *box, m, last, &levelCollider, &body, isects.data(), found, impacts.data(),
                                   kMaxImpacts);
    ASSERT_GT(n, 0);
    for (int i = 0; i < n; ++i) {
        const Impact& im = impacts[static_cast<std::size_t>(i)];
        EXPECT_EQ(im.colliderA, &levelCollider);
        EXPECT_EQ(im.colliderB, &body);
        EXPECT_EQ(im.componentA, 1);
        EXPECT_NEAR(im.normal.y, -1.0f, 1e-4f);
        calcImpact(impacts[static_cast<std::size_t>(i)], 1.0f / static_cast<float>(n));
    }
    // The box (B) takes the upward impulse and the push.
    EXPECT_GT(ics.linearImpulse.y, 0.0f);
    EXPECT_GT(ics.linearPush.y, 0.0f);
}

TEST(World, BoxComesToRestOnTheLevel) {
    // dgPhysManager::CollideTerrain each sample: a box dropped on the floor
    // stays on it. MM2 resolves the four corner impacts of a flat landing
    // with a quarter weight each, so a resting box keeps a small downward
    // velocity that the sample's push cancels: its velocity with the pushes
    // (what phSleep looks at) is about zero.
    fixtures::TestLevel level;
    level.floor(100.0f);
    World world;
    world.setLevel(&level);
    BoxBody box({1, 1, 1}, 100.0f, {0, 2.0f, 0}, 0.3f, 0.8f);
    world.add(&box.body);
    for (int i = 0; i < 600; ++i) {
        world.step(1.0f / 60.0f);
        ASSERT_GT(box.body.ics.matrix.m3.y, 0.45f) << i;
    }
    EXPECT_NEAR(box.body.ics.matrix.m3.y, 0.5f, 0.03f);
    EXPECT_LT(box.body.ics.frameVelocity.mag(), 0.05f);
    EXPECT_NEAR(box.body.ics.angularVelocity.mag(), 0.0f, 1e-3f);
}

namespace {

// The vertical velocity after the first sample that collided with the floor.
float velocityAfterLanding(float elasticity) {
    fixtures::TestLevel level;
    level.floor(100.0f);
    World world;
    world.setLevel(&level);
    BoxBody box({1, 1, 1}, 100.0f, {0, 1.5f, 0}, elasticity, 0.8f);
    world.add(&box.body);
    for (int i = 0; i < 240; ++i) {
        world.step(1.0f / 120.0f);
        if (world.stats().terrainImpacts > 0) {
            world.step(1.0f / 120.0f); // the impulses act at the next integration
            return box.body.ics.linearVelocity.y;
        }
    }
    return 0.0f;
}

} // namespace

TEST(World, ElasticityDecidesTheRebound) {
    // The floor's default material (elasticity 0.5) times the box's: 0
    // stops less of the fall than 0.5 * 1 does.
    const float dull = velocityAfterLanding(0.0f);
    const float lively = velocityAfterLanding(1.0f);
    EXPECT_LT(dull, 0.0f);
    EXPECT_GT(lively, dull + 0.5f);
}

TEST(World, MoversExchangeMomentum) {
    // Two movers, box against box: momentum is conserved, the struck one
    // moves off.
    World world;
    BoxBody a({1, 1, 1}, 100.0f, {-1.2f, 5, 0}, 0.5f, 0.0f);
    BoxBody b({1, 1, 1}, 100.0f, {0, 5, 0}, 0.5f, 0.0f);
    a.body.ics.gravity = {0, 0, 0};
    b.body.ics.gravity = {0, 0, 0};
    a.body.ics.linearVelocity = {10, 0, 0};
    a.body.ics.linearMomentum = {1000, 0, 0};
    world.add(&a.body);
    world.add(&b.body);
    for (int i = 0; i < 30; ++i)
        world.step(1.0f / 60.0f);
    const float p = a.body.ics.linearMomentum.x + b.body.ics.linearMomentum.x;
    EXPECT_NEAR(p, 1000.0f, 1.0f);
    EXPECT_GT(b.body.ics.linearVelocity.x, 4.0f);
    EXPECT_LT(a.body.ics.linearVelocity.x, 6.0f);
}

TEST(World, JointedBodiesDoNotCollide) {
    World world;
    BoxBody a({1, 1, 1}, 100.0f, {-1.2f, 5, 0});
    BoxBody b({1, 1, 1}, 100.0f, {0, 5, 0});
    Joint joint;
    a.body.joint = &joint;
    b.body.joint = &joint;
    a.body.ics.gravity = {0, 0, 0};
    b.body.ics.gravity = {0, 0, 0};
    a.body.ics.linearVelocity = {10, 0, 0};
    a.body.ics.linearMomentum = {1000, 0, 0};
    world.add(&a.body);
    world.add(&b.body);
    for (int i = 0; i < 30; ++i)
        world.step(1.0f / 60.0f);
    EXPECT_EQ(b.body.ics.linearVelocity.x, 0.0f);
}

TEST(World, BoxRestsOnATerrainInstance) {
    // An object with a terrain bound of its own space (phBoundTerrainLocal,
    // an lvlInstance with flag 0x100): bodies collide with it through
    // CollideInstances.
    GeometryData g;
    g.vertices = {{-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10}};
    GeometryData::Poly quad;
    quad.v = {0, 1, 2, 3};
    quad.quad = true;
    g.polys.push_back(quad);
    auto plain = makeGeometryBound(g);
    ASSERT_TRUE(plain);
    TerrainData td;
    td.grid.widthSections = 1;
    td.grid.heightSections = 1;
    td.grid.depthSections = 1;
    td.grid.size = plain->boxMax - plain->boxMin;
    td.grid.sectionOffsets = {0};
    td.grid.sectionCounts = {1};
    td.grid.sectionPolygons = {0};
    td.grid.sectionSizeFactors = {1.0f / td.grid.size.x, 1.0f, 1.0f / td.grid.size.z};
    td.boxMin = plain->boxMin;
    td.boxMax = plain->boxMax;
    td.edges = plain->edges;
    for (const Polygon& p : plain->polygons)
        td.polygonEdges.push_back({p.edges[0], p.edges[1], p.edges[2], p.edges[3]});
    td.edgeNormals = plain->edgeNormals;
    td.edgeCosines = plain->edgeCosines;
    auto terrain = makeTerrainBound(g, &td, true);
    ASSERT_TRUE(terrain);
    Mat34 at = Mat34::identity();
    at.m3 = {0, 1.0f, 0};
    fixtures::TestInstance inst(terrain.get(), at);
    inst.terrainCollidable = true;
    fixtures::TestLevel level; // no polygons of its own
    level.objects.push_back(&inst);
    World world;
    world.setLevel(&level);
    BoxBody box({1, 1, 1}, 100.0f, {0, 2.5f, 0}, 0.3f, 0.8f);
    world.add(&box.body);
    for (int i = 0; i < 600; ++i)
        world.step(1.0f / 60.0f);
    EXPECT_NEAR(box.body.ics.matrix.m3.y, 1.5f, 0.05f);
}

namespace {

// vehCarDamage on a car at rest: impacts from `other`.
struct DamageRig {
    CarSim car;
    std::unique_ptr<BoundBox> otherBound = ownBox({1, 1, 1}, 0.5f, 1.0f);
    InertialCS otherIcs;
    Mat34 otherMatrix = Mat34::identity();
    Collider other;
    std::vector<CarImpact> events;

    explicit DamageRig(bool otherIsBody) {
        car.init(CarSimParams{}, VehicleGeometry::placeholder());
        car.reset(Mat34::identity());
        CarDamageParams p;
        p.impactThreshold = 1500.0f;
        car.setDamageParams(p);
        otherIcs.setMass(1, 1, 1, car.body.ics.mass);
        other.init(otherBound.get(), &otherMatrix, otherIsBody ? &otherIcs : nullptr);
        car.onImpactCallback = [this](const CarImpact& e) { events.push_back(e); };
    }
    void hit(float impulse) {
        Impact im;
        im.colliderA = &car.body.collider;
        im.colliderB = &other;
        im.position = {0, 0.5f, -2};
        im.normal = {0, 0, 1};
        car.onImpact(car.body.collider, im, {0, 0, impulse});
    }
};

} // namespace

TEST(CarDamage, ImpactValueIsTheOtherBodysMassShare) {
    // vehCarDamage::InsertImpact: |impulse| * the other's share of the
    // masses; equal masses halve it.
    DamageRig rig(true);
    rig.hit(4000.0f);
    ASSERT_EQ(rig.events.size(), 1u);
    EXPECT_FLOAT_EQ(rig.events[0].value, 2000.0f);
    EXPECT_TRUE(rig.events[0].sound);
    EXPECT_FLOAT_EQ(rig.events[0].soundStrength, 4000.0f);
    EXPECT_TRUE(rig.events[0].damaging); // against a body, whatever the speed
    EXPECT_FLOAT_EQ(rig.car.damage.currentDamage, 2000.0f);
    EXPECT_EQ(rig.car.stuck.state, Stuck::Watching); // vehStuck::Impact first
}

TEST(CarDamage, RepeatedImpactsNeedAQuarterMoreToRetrigger) {
    DamageRig rig(true);
    rig.hit(4000.0f); // value 2000: applied
    rig.hit(4400.0f); // 2200 <= 1.25 * 2000: damage only, silently
    EXPECT_EQ(rig.events.size(), 1u);
    EXPECT_FLOAT_EQ(rig.car.damage.currentDamage, 4200.0f);
    rig.hit(6000.0f); // 3000 > 2500: applied again
    ASSERT_EQ(rig.events.size(), 2u);
    EXPECT_FLOAT_EQ(rig.events[1].total, 2000.0f + 2200.0f + 3000.0f);
    // RelaxTime (0.2 s) later the collider leaves the list.
    for (int i = 0; i < 15; ++i)
        rig.car.damage.update(1.0f / 60.0f);
    rig.hit(4000.0f);
    EXPECT_EQ(rig.events.size(), 3u);
}

TEST(CarDamage, WorldImpactsDamageOnlyAtSpeed) {
    // Against something that does not move, an impact above the threshold
    // damages only at 10 mph or more (the car is at rest here); it still
    // sounds above 0.001.
    DamageRig rig(false);
    rig.hit(4000.0f);
    ASSERT_EQ(rig.events.size(), 1u);
    EXPECT_FLOAT_EQ(rig.events[0].value, 4000.0f);
    EXPECT_TRUE(rig.events[0].sound);
    EXPECT_FALSE(rig.events[0].damaging);
    EXPECT_EQ(rig.car.damage.currentDamage, 0.0f);
    // Too soft to be heard.
    for (int i = 0; i < 15; ++i)
        rig.car.damage.update(1.0f / 60.0f);
    rig.hit(0.0005f);
    EXPECT_EQ(rig.events.size(), 1u);
}

TEST(World, CarHittingACarAtElevenMetresASecondStaysDown) {
    // The reported case: an opponent (a box, as AI cars collide) running into
    // a stopped traffic car (a box) at 11 m/s on flat ground. MM2's response
    // neither throws it up nor lets it pass through.
    fixtures::TestLevel level;
    level.floor(200.0f);
    World world;
    world.setLevel(&level);
    BoxBody car({1.9f, 1.4f, 4.4f}, 1300.0f, {0, 0.71f, 10.0f}, 0.2f, 0.3f);
    BoxBody traffic({2.0f, 1.5f, 4.6f}, 1500.0f, {0, 0.76f, 0.0f});
    car.body.ics.linearVelocity = {0, 0, -11.0f};
    car.body.ics.linearMomentum = {0, 0, -11.0f * 1300.0f};
    world.add(&car.body);
    world.add(&traffic.body);
    float maxY = 0.0f, pushed = 0.0f;
    for (int i = 0; i < 180; ++i) {
        world.step(1.0f / 60.0f);
        maxY = std::max(maxY, car.body.ics.matrix.m3.y);
        pushed = std::min(pushed, traffic.body.ics.linearVelocity.z);
    }
    EXPECT_LT(maxY, 0.8f);
    EXPECT_GT(car.body.ics.matrix.m3.z, traffic.body.ics.matrix.m3.z + 4.4f); // still behind it
    EXPECT_LT(pushed, -2.0f);                                                  // shoved along
}
