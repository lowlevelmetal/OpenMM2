// Text data readers checked against MM2's loaders (docs/parity/formats.md).
#include "data/CNumbers.h"
#include "data/DatFile.h"
#include "data/TextTables.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(ParityNumbers, AtofAndAtoiReadTheNumericPrefix) {
    EXPECT_DOUBLE_EQ(data::cAtof("1.#QNAN0"), 1.0);
    EXPECT_DOUBLE_EQ(data::cAtof("  -2.5e1xyz"), -25.0);
    EXPECT_DOUBLE_EQ(data::cAtof(".5"), 0.5);
    EXPECT_DOUBLE_EQ(data::cAtof("1e"), 1.0);
    EXPECT_DOUBLE_EQ(data::cAtof("abc"), 0.0);
    EXPECT_FALSE(data::atofPrefix("-"));
    EXPECT_EQ(data::cAtoi("0x10"), 0);
    EXPECT_EQ(data::cAtoi("12px"), 12);
    EXPECT_EQ(data::cAtoi(" +7"), 7);
    EXPECT_EQ(data::cAtoi("1.9"), 1);
    EXPECT_EQ(data::cAtoi("4294967295"), -1); // wraps like the 32-bit runtime
}

TEST(ParityDatFile, TokensAsDatBaseTokenizerSplitsThem) {
    const char* text = "type: a\r\n"
                       "aiVehicleData {\r\n"
                       "  MaxAng 1.#QNAN0\t0.000000\t0.000000 \r\n"
                       "  Approach Rate 1.2\r\n"
                       "  Aero asAero :075abc8c {\r\n"
                       "    Drag 0.5 ; comment\r\n"
                       "  }\r\n"
                       "  Mass 1\r\n"
                       "  Mass 2.9\r\n"
                       "}\r\n"
                       "Ignored { Mass 3 }\r\n";
    std::string err;
    auto f = data::parseDat(text, &err);
    ASSERT_TRUE(f) << err;
    const auto* top = f->top();
    ASSERT_TRUE(top);
    // atof reads "1.#QNAN0" as 1.
    EXPECT_FLOAT_EQ(top->getVec3("MaxAng")->x, 1.0f);
    // A name with a space never matches a token.
    EXPECT_FALSE(top->getFloat("Approach Rate"));
    // A labelled block ("Aero asAero :address {") is still the Aero block.
    ASSERT_TRUE(top->child("Aero"));
    EXPECT_FLOAT_EQ(*top->child("Aero")->getFloat("Drag"), 0.5f);
    // The last occurrence wins; integers are atoi of the token.
    EXPECT_FLOAT_EQ(*top->getFloat("Mass"), 2.9f);
    EXPECT_EQ(*top->getInt("Mass"), 2);
    // Reading stops at the class block's closing brace.
    EXPECT_EQ(f->root.children.size(), 1u);
}

TEST(ParityDatFile, HeaderIsSevenBytes) {
    // GetReadTokenizer consumes seven bytes whatever they are (as with the
    // retail snow.asbirthrule, which has no header).
    auto f = data::parseDat("asBirthRule {\n Life 1\n}\n");
    ASSERT_TRUE(f);
    ASSERT_TRUE(f->top());
    EXPECT_EQ(f->top()->name, "Rule");
    EXPECT_FLOAT_EQ(*f->top()->getFloat("Life"), 1.0f);
    EXPECT_FALSE(data::parseDat("type: b\n"));
}

TEST(ParityTextTables, NumbersUseTheCRuntimeRules) {
    auto kv = data::KeyValueFile::parse("Top Speed=124 mph\nScoringBias=20.0x\nOrder=0x10\n");
    EXPECT_EQ(kv.getInt("Top Speed"), 124);
    EXPECT_FLOAT_EQ(kv.getFloat("ScoringBias"), 20.0f);
    EXPECT_EQ(kv.getInt("Order"), 0);
    EXPECT_EQ(kv.getInt("Missing", 5), 5);

    auto csv = data::CsvTable::parse("a,b\n1.5m,x\n");
    EXPECT_FLOAT_EQ(csv.cellFloat(0, 0), 1.5f);
    EXPECT_EQ(csv.cellInt(0, 1, 9), 0); // atoi of text without digits
    EXPECT_EQ(csv.cellInt(0, 5, 9), 9); // missing cell
}
