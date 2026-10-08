// .vehCarSim files read with vehCarSim's record list, as datParser::Read
// reads them (docs/parity/formats.md).
#include "TestData.h"
#include "data/DatFile.h"
#include "phys/vehicle/TuneParams.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <type_traits>

using namespace mm2;

namespace {

std::string readText(const std::string& path) {
    auto bytes = test::gameData()->readAll(path);
    return bytes ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string();
}

} // namespace

// The fire truck's tune has MM1-era fields and particle rules that
// vehCarSim does not register. MM2 skips only the first line of each rule,
// so the first rule's "Mass 0.1" becomes the car's mass and its closing
// brace ends the vehCarSim block; in Trans the unregistered gear lists
// swallow "ManualNumGears 8".
TEST(ParityCarSimTune, FireTruckReadsAsMm2ReadsIt) {
    MM2_REQUIRE_GAME_DATA();
    std::string err;
    auto f = data::parseDat(readText("tune/vehicle/vpftruck.vehcarsim"), phys::carSimSchema(), &err);
    ASSERT_TRUE(f) << err;
    const auto* top = f->top();
    ASSERT_TRUE(top);
    EXPECT_FLOAT_EQ(*top->getFloat("Mass"), 0.1f);
    const auto* trans = top->child("Trans");
    ASSERT_TRUE(trans);
    EXPECT_FALSE(trans->child("ManualNumGears"));
    ASSERT_TRUE(top->child("Aero"));
    EXPECT_FLOAT_EQ(top->child("Aero")->getVec3("AngVelDamp")->x, 6.0f);
}

// Every other tune MM2 loads (the base cars; it never loads the _opp and
// _cop variants) reads the same with and without the record list. The six
// variants with labelled particle rules read like the fire truck.
TEST(ParityCarSimTune, OtherRetailTunesAreUnaffected) {
    MM2_REQUIRE_GAME_DATA();
    auto cop = data::parseDat(readText("tune/vehicle/vpcop_cop.vehcarsim"), phys::carSimSchema());
    ASSERT_TRUE(cop && cop->top());
    EXPECT_FLOAT_EQ(*cop->top()->getFloat("Mass"), 0.1f);

    int compared = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!e.path.starts_with("tune/vehicle/") || !e.path.ends_with(".vehcarsim") ||
            e.path.find("vpftruck") != std::string::npos || e.path.find("_opp.") != std::string::npos ||
            e.path.find("_cop.") != std::string::npos)
            continue;
        const std::string text = readText(e.path);
        auto exact = data::parseDat(text, phys::carSimSchema());
        auto tree = data::parseDat(text);
        ASSERT_TRUE(exact && tree) << e.path;
        phys::CarSimParams a, b;
        ASSERT_TRUE(phys::loadCarSimParams(*exact->top(), a)) << e.path;
        ASSERT_TRUE(phys::loadCarSimParams(*tree->top(), b)) << e.path;
        // Plain floats and ints with no padding: compare every field at once.
        static_assert(std::is_trivially_copyable_v<phys::CarSimParams>);
        EXPECT_EQ(std::memcmp(&a, &b, sizeof a), 0) << e.path;
        ++compared;
    }
    EXPECT_GT(compared, 20);
}
