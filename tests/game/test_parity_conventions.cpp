// Round 3 of the parity audit, conventions: values that cross from MM2's
// data files into the game keep their units, signs and axes (MM2Recomp,
// build 3393). See docs/parity/round3/conventions.md.

#include "TestData.h"
#include "city/CityData.h"
#include "game/Strings.h"
#include "game/session/RaceSetup.h"
#include "game/session/Session.h"
#include "phys/vehicle/CarSim.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>

using namespace mm2;
using namespace mm2::game;
using namespace mm2::game::session;

namespace {

struct ConventionsRetail {
    city::CityData london;
    Strings strings;
};

ConventionsRetail* conventionsRetail() {
    static std::unique_ptr<ConventionsRetail> r = []() -> std::unique_ptr<ConventionsRetail> {
        if (!test::gameData())
            return nullptr;
        auto src = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
        auto london = city::loadCity(*test::gameData(), "london");
        if (!src || !london)
            return nullptr;
        auto out = std::make_unique<ConventionsRetail>();
        out->london = std::move(*london);
        out->strings = Strings::load(*src);
        return out;
    }();
    return r.get();
}

std::unique_ptr<Session> londonSession(GameMode mode, int index) {
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.city = "london";
    cfg.raceIndex = index;
    SessionOptions options;
    options.seed = 11;
    std::string error;
    auto s = Session::create(cfg, conventionsRetail()->london, *test::gameData(), conventionsRetail()->strings,
                             &error, options);
    EXPECT_TRUE(s) << error;
    return s;
}

} // namespace

// aiRaceData::aiRaceData: a [Police] heading in degrees becomes the reset
// rotation heading x -0.017444445 (not pi / 180). London's checkpoint race
// 3 posts a cop at -195 degrees.
TEST(ConventionsParity, PoliceHeadingUsesMM2DegreeFactor) {
    MM2_REQUIRE_GAME_DATA();
    if (!conventionsRetail())
        GTEST_SKIP() << "retail data incomplete";
    auto s = londonSession(GameMode::Checkpoint, 3);
    ASSERT_TRUE(s);
    ASSERT_GE(s->police().size(), 2u);
    const Mat34& post = s->police()[0].spawn;
    const float a = -195.0f * -0.017444445f;
    EXPECT_FLOAT_EQ(post.m2.x, std::sin(a));
    EXPECT_FLOAT_EQ(post.m2.z, std::cos(a));
    EXPECT_FLOAT_EQ(post.m3.x, 250.610474f);
    EXPECT_FLOAT_EQ(post.m3.z, 521.594055f);
    // The reset rotation the car gets back from the post is that angle
    // (wrapped into (-pi, pi]).
    EXPECT_NEAR(phys::resetRotationOf(post), a - 2.0f * kPi, 1e-5f);
}
