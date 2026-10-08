// dgPhysManager::DeclareMover of an instance without a body (MM2Recomp,
// build 3393): the ambient cars off their rails (aiGoalAvoidPlayer,
// aiGoalRegainRail, aiGoalCollision for a wreck) collide with the instances
// round them for the frame they are declared. See docs/parity/mm2/ai.md.
#include "phys/Bound.h"
#include "phys/World.h"

#include <gtest/gtest.h>

#include <vector>

using namespace mm2;

namespace {

// A box instance that never takes a body: it counts the AttachEntity calls
// the collisions make.
class StaticCar final : public phys::Instance {
public:
    explicit StaticCar(const Vec3& position) {
        m_matrix = Mat34::identity();
        m_matrix.m3 = position;
        m_box.makeOwnMaterial();
        m_box.setSize({2.0f, 1.5f, 4.5f});
        room = 1;
    }
    const phys::Bound* bound(int) const override { return &m_box; }
    const Mat34& matrix() const override { return m_matrix; }
    float radius() const override { return 3.0f; }
    phys::Body* attachEntity() override {
        ++attaches;
        return nullptr;
    }

    int attaches = 0;

private:
    Mat34 m_matrix;
    phys::BoundBox m_box;
};

// One room listing the instances; no city polygons.
class OneRoom final : public phys::Level {
public:
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override { out.clear(); }
    void instances(int room, std::vector<phys::Instance*>& out) const override {
        if (room == 1)
            out.insert(out.end(), listed.begin(), listed.end());
    }
    std::vector<phys::Instance*> listed;
};

} // namespace

// An instance declared (2, 0x08) collides with an overlapping instance of
// its room: each side without a body is asked for one (AttachEntity), as
// for any static side of CollideInstances; undeclared, nothing happens, and
// a declaration lasts one frame.
TEST(ParityMm2AiMovers, InstanceWithoutABodyCollidesForTheFrameItIsDeclared) {
    StaticCar swerving({0.0f, 0.0f, 0.0f});
    StaticCar parked({1.0f, 0.0f, 0.5f});
    OneRoom level;
    level.listed = {&swerving, &parked};
    phys::World world;
    world.setLevel(&level);

    world.advanceFixed(phys::kFixedSampleStep);
    EXPECT_EQ(parked.attaches, 0);
    EXPECT_EQ(swerving.attaches, 0);

    world.declareInstance(&swerving, 2, 0x08);
    world.advanceFixed(phys::kFixedSampleStep);
    EXPECT_GT(parked.attaches, 0);
    EXPECT_GT(swerving.attaches, 0);

    const int seen = parked.attaches;
    world.advanceFixed(phys::kFixedSampleStep); // not declared again
    EXPECT_EQ(parked.attaches, seen);
}

// DeclareMover refuses an instance outside every room.
TEST(ParityMm2AiMovers, InstanceOutsideTheRoomsIsNotDeclared) {
    StaticCar swerving({0.0f, 0.0f, 0.0f});
    StaticCar parked({1.0f, 0.0f, 0.5f});
    swerving.room = 0;
    OneRoom level;
    level.listed = {&parked};
    phys::World world;
    world.setLevel(&level);
    world.declareInstance(&swerving, 2, 0x0a);
    world.advanceFixed(phys::kFixedSampleStep);
    EXPECT_EQ(parked.attaches, 0);
}
