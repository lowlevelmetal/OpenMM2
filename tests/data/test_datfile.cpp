#include "TestData.h"
#include "data/DatFile.h"
#include "data/TextTables.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(DatFile, ParsesNestedBlocksAndArrays) {
    const char* text = "type: a\r\n"
                       "vehCarSim {\r\n"
                       "  Mass 1000.000000 \r\n"
                       "  InertiaBox 2.0\t2.0\t3.0 \r\n"
                       "  Trans {\r\n"
                       "    GearRatios -20 0 28\n"
                       "        20 16\n"
                       "    UpshiftRPM\n"
                       "        6000\n"
                       "  }\n"
                       "  Approach Rate 1.2\n"
                       "  Name \"hello world\"\n"
                       "}\n";
    std::string err;
    auto f = data::parseDat(text, &err);
    ASSERT_TRUE(f) << err;
    const auto* top = f->top();
    ASSERT_TRUE(top);
    EXPECT_EQ(top->name, "vehCarSim");
    EXPECT_FLOAT_EQ(*top->getFloat("Mass"), 1000.0f);
    EXPECT_EQ(top->getVec3("InertiaBox")->z, 3.0f);
    const auto* trans = top->child("Trans");
    ASSERT_TRUE(trans);
    EXPECT_EQ(trans->getFloats("GearRatios").size(), 5u);
    EXPECT_EQ(trans->getFloats("UpshiftRPM").size(), 1u);
    // MM2's tokenizer cannot match a field name with a space in it.
    EXPECT_FALSE(top->getFloat("Approach Rate"));
    EXPECT_EQ(*top->getString("Name"), "hello world");
}

TEST(DatFile, RejectsUnbalancedBraces) {
    EXPECT_FALSE(data::parseDat("type: a\nfoo {\n Bar 1\n"));
    EXPECT_FALSE(data::parseDat("type: a\n}\n"));
}

TEST(TextTables, KeyValueAndCsv) {
    auto kv = data::KeyValueFile::parse("BaseName=vpbug\r\nTop Speed=91 \t\r\nColors=Yellow|Blue\r\n");
    EXPECT_EQ(kv.getString("basename"), "vpbug");
    EXPECT_EQ(kv.getInt("Top Speed"), 91);
    EXPECT_EQ(kv.getList("Colors").size(), 2u);

    auto csv = data::CsvTable::parse("x,y,z\n1,2,3\n4, 5 ,6\n");
    ASSERT_EQ(csv.rows().size(), 2u);
    EXPECT_EQ(csv.column("Y"), 1);
    EXPECT_FLOAT_EQ(csv.cellFloat(1, 1), 5.0f);
}

// Every "type: a" file in the retail archives must parse.
TEST(DatFile, ParsesAllRetailTuneFiles) {
    MM2_REQUIRE_GAME_DATA();
    int parsed = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!e.path.starts_with("tune/"))
            continue;
        auto bytes = test::gameData()->readAll(e.path);
        ASSERT_TRUE(bytes) << e.path;
        std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (!text.starts_with("type: a"))
            continue;
        std::string err;
        auto f = data::parseDat(text, &err);
        EXPECT_TRUE(f) << e.path << ": " << err;
        if (f) {
            EXPECT_TRUE(f->top()) << e.path;
        }
        ++parsed;
    }
    EXPECT_GT(parsed, 1000);
}
