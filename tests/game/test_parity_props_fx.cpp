// Parity checks of the props and particle effects against MM2's own code
// (MM2Recomp, build 3393), audited from MM2's side: vehBreakableMgr::Reset
// with dgHitBangerInstance::Detach, dgBangerInstance::DrawGlow's standing
// flag, dgBangerActive::Attach's debris sheet and kept rule,
// lvlLevel::MoveToRoom's list order. See docs/parity/mm2/props-fx.md.
#include "TestData.h"
#include "game/bangers/BangerData.h"
#include "game/bangers/BangerSet.h"
#include "game/fx/EffectLibrary.h"
#include "phys/World.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mm2;
using namespace mm2::game::bangers;

namespace {

constexpr float kDt = 1.0f / 60.0f;

// A temporary game folder with a few banger files.
struct Files {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                (std::string("openmm2_parity_props_fx_") +
                                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
    vfs::Vfs vfs;
    Files() {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "tune" / "banger");
        write("lamp", "  TexNumber 0\n");
        // A mailbox: 10 letters at once, then 60 a second for ever.
        write("mailbox", "  TexNumber 7\n",
              "    Life 100\n    InitialBlast 10\n    SpewRate 60\n    SpewTimeLimit 0\n    Gravity 0\n");
        vfs.mount(std::make_shared<vfs::DirectoryFs>(dir));
    }
    ~Files() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    void write(const char* name, const char* extra, const char* rule = "    InitialBlast 0\n") {
        std::ofstream f(dir / "tune" / "banger" / (std::string(name) + ".dgbangerdata"));
        f << "type: a\ndgBangerData {\n  Size 0.5 2 0.5\n  CG 0 1 0\n  Mass 30\n  ImpulseLimit2 1e6\n";
        f << "  BirthRule {\n" << rule << "  }\n" << extra << "  CollisionPrim 1\n}\n";
    }
};

// One room with a floor whose objects are the set's props.
class OneRoom final : public phys::Level {
public:
    explicit OneRoom(const game::InstanceSource& source) : m_source(source) {}
    int findRoom(const Vec3&, int) const override { return 1; }
    int touchedNeighbors(int*, int, int, const Vec3&, float) const override { return 0; }
    void collect(const int*, int, const Vec3&, float, phys::LevelBound& out) const override {
        out.clear();
        const Vec3 floor[4] = {{-500, 0, -500}, {-500, 0, 500}, {500, 0, 500}, {500, 0, -500}};
        out.addPolygon(floor, 4, {0, 1, 0}, 0);
    }
    void instances(int room, std::vector<phys::Instance*>& out) const override { m_source.instancesIn(room, out); }

private:
    const game::InstanceSource& m_source;
};

struct Rig {
    Files files;
    BangerDataLibrary lib{files.vfs};
    phys::World world;
    BangerSet set{lib};
    OneRoom level{set};
    Rig() {
        world.setLevel(&level);
        set.setWorld(&world);
    }
    ~Rig() { set.setWorld(nullptr); }
    PlacedProp placed(const char* model, const Vec3& at) {
        return {model, Mat34::translation(at), 1, PlacedProp::Source::Instance, true};
    }
    std::vector<phys::Instance*> listed() const {
        std::vector<phys::Instance*> out;
        set.instancesIn(1, out);
        return out;
    }
};

} // namespace

// vehBreakableMgr::Reset (vehCarModel::ClearDamage) calls the ejected part's
// hit instance's Detach: whether it still flies (its active detaches) or
// lies at rest, it leaves its room and disappears. A placed prop keeps
// lvlInstance's empty Detach.
TEST(ParityPropsFx, ClearedCarDamageTakesTheEjectedPartsAway) {
    Rig r;
    r.set.add({r.placed("lamp", {20, 0, 0})});
    const BangerData* d = r.lib.find("lamp");
    ASSERT_TRUE(d);
    const std::size_t flying = r.set.ejectPart(*d, "vpcar", "WHL0", 0, Mat34::translation({0, 1, 0}), 4.0f);
    EXPECT_EQ(flying, 1u); // the ring's first hit instance, after the placed prop
    EXPECT_EQ(r.set.instances()[flying].state, BangerSet::State::Active);
    const phys::Body* body = r.set.body(flying);
    ASSERT_TRUE(body);
    r.set.detachHit(flying);
    EXPECT_EQ(r.set.instances()[flying].state, BangerSet::State::Gone);
    EXPECT_EQ(r.set.instances()[flying].room, 0);
    EXPECT_EQ(r.set.activeCount(), 0);
    EXPECT_FALSE(r.world.contains(body));

    // One that came to rest: a collidable hit instance until it is detached.
    const std::size_t resting = r.set.ejectPart(*d, "vpcar", "WHL1", 0, Mat34::translation({0, 1, 0}), 0.0f);
    for (int i = 0; i < 1200 && r.set.activeCount() > 0; ++i) {
        r.world.step(kDt);
        r.set.update(kDt);
    }
    ASSERT_EQ(r.set.instances()[resting].state, BangerSet::State::Hit);
    EXPECT_EQ(r.listed().size(), 2u);
    r.set.detachHit(resting);
    EXPECT_EQ(r.set.instances()[resting].state, BangerSet::State::Gone);
    ASSERT_EQ(r.listed().size(), 1u);
    EXPECT_EQ(r.listed()[0], &r.set.prop(0));

    // The placed prop stays.
    r.set.detachHit(0);
    EXPECT_EQ(r.set.instances()[0].state, BangerSet::State::Unhit);
    EXPECT_EQ(r.listed().size(), 1u);
}

// dgBangerInstance::DrawGlow tests lvlInstance flag 1: it stays while an
// active holds the prop and only goes when dgUnhitBangerInstance::Impact
// breaks it loose; dgUnhitBangerInstance::Reset sets it again.
TEST(ParityPropsFx, AHeldLampStillStandsAndGlows) {
    Rig r;
    r.set.add({r.placed("lamp", {0, 0, 0})});
    EXPECT_TRUE(r.set.standing(0));
    ASSERT_TRUE(r.set.prop(0).attachEntity());
    EXPECT_EQ(r.set.instances()[0].state, BangerSet::State::Active);
    EXPECT_TRUE(r.set.standing(0));
    // Broken loose: the prop is gone and its hit instance never stands.
    r.set.prop(0).bangerHit(r.set.prop(0), {});
    EXPECT_FALSE(r.set.standing(0));
    ASSERT_EQ(r.set.instances().size(), 2u);
    EXPECT_FALSE(r.set.standing(1));
    r.set.reset();
    EXPECT_TRUE(r.set.standing(0));
}

// dgBangerActive::Attach: TexNumber - 1 clamped to 0..20 picks one of
// dgBangerDataManager's 20 fxpt sheets; 0 has no debris.
TEST(ParityPropsFx, DebrisSheetFollowsTexNumber) {
    using game::fx::EffectLibrary;
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(0), 0);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(-3), 1);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(1), 1);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(7), 7);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(16), 16);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(20), 20);
    EXPECT_EQ(EffectLibrary::bangerSheetNumber(21), 0);
}

// dgBangerActive::Attach of a prop without a debris sheet only resets the
// particles: the active keeps the rule and sheet it last had, so a reused
// active that held a mailbox goes on spewing its letters, without a birth
// matrix (around the world origin).
TEST(ParityPropsFx, AReusedActiveKeepsItsLastDebrisRule) {
    Rig r;
    r.set.add({r.placed("mailbox", {50, 0, 50}), r.placed("lamp", {80, 0, 80})});
    ASSERT_TRUE(r.set.prop(0).attachEntity());
    auto debris = r.set.debris(0);
    ASSERT_TRUE(debris);
    EXPECT_EQ(debris->sheet, 7);
    EXPECT_EQ(debris->particles->count(), 10); // InitialBlast, born at the mailbox
    EXPECT_GT(debris->particles->positions()[0].position.x, 40.0f);

    r.set.reset(); // every active back in the pool, in order
    ASSERT_TRUE(r.set.prop(1).attachEntity());
    debris = r.set.debris(1);
    ASSERT_TRUE(debris);
    EXPECT_EQ(debris->particles->count(), 0); // reset ...
    EXPECT_EQ(debris->sheet, 7);              // ... but the mailbox's sheet stays
    r.set.update(kDt);
    debris = r.set.debris(1);
    ASSERT_TRUE(debris);
    ASSERT_EQ(debris->particles->count(), 1); // SpewRate 60 for 1/60 s
    EXPECT_LT(std::abs(debris->particles->positions()[0].position.x), 1.0f);
    EXPECT_LT(std::abs(debris->particles->positions()[0].position.z), 1.0f);
}

// dgBangerData::Load reads its rule with asBirthRule::Load, which takes the
// 24 fields dgBangerData::Save writes: LifeVar, Damp, DampVar, Height,
// Intensity and Color keep the asBirthRule defaults.
TEST(ParityPropsFx, BangerRulesReadOnlyTheSavedFields) {
    const auto d = parseBangerData("sp_test", "type: a\ndgBangerData {\n  BirthRule {\n    Life 2\n    LifeVar 1\n"
                                              "    Damp 0.5\n    DampVar 0.2\n    Height 3\n    Intensity 0.5\n"
                                              "    Color -331546\n    SpewRate 4\n    BirthFlags 2\n  }\n"
                                              "  TexNumber 3\n}\n");
    ASSERT_TRUE(d);
    ASSERT_TRUE(d->birthRule);
    const game::fx::BirthRule defaults;
    EXPECT_FLOAT_EQ(d->birthRule->life, 2.0f);
    EXPECT_FLOAT_EQ(d->birthRule->spewRate, 4.0f);
    EXPECT_EQ(d->birthRule->birthFlags, 2);
    EXPECT_FLOAT_EQ(d->birthRule->lifeVar, defaults.lifeVar);
    EXPECT_FLOAT_EQ(d->birthRule->damp, defaults.damp);
    EXPECT_FLOAT_EQ(d->birthRule->dampVar, defaults.dampVar);
    EXPECT_FLOAT_EQ(d->birthRule->height, defaults.height);
    EXPECT_FLOAT_EQ(d->birthRule->intensity, defaults.intensity);
    EXPECT_EQ(d->birthRule->color, defaults.color);
}

// lvlLevel::MoveToRoom puts an instance at the head of its room's list.
TEST(ParityPropsFx, RoomsListTheirPropsNewestFirst) {
    Rig r;
    r.set.add({r.placed("lamp", {0, 0, 0}), r.placed("lamp", {5, 0, 0})});
    const auto list = r.listed();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], &r.set.prop(1));
    EXPECT_EQ(list[1], &r.set.prop(0));
}
