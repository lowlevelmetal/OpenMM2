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

namespace {

// A class with a float, an int, a vector and a nested parser, as a FileIO
// registers them.
const data::DatSchema& testSchema() {
    using R = data::DatRecord;
    using T = R::Type;
    static const data::DatSchema schema = {
        {"Mass", T::Float, 1, {}},
        {"Count", T::Int, 1, {}},
        {"Box", T::Vec3, 1, {}},
        {"Aero", T::Parser, 1, {{"Drag", T::Float, 1, {}}}},
    };
    return schema;
}

} // namespace

// datParser::Read: an unknown name skips the rest of its line, so the lines
// of an unknown labelled block are read as fields of the outer class and its
// closing brace ends the outer block.
TEST(ParityDatFile, UnknownLabelledBlockSkipsOnlyItsFirstLine) {
    const char* text = "type: a\r\n"
                       "vehCarSim {\r\n"
                       "  Mass 4500\r\n"
                       "  Aero asAero :075abc8c {\r\n" // a registered parser skips its own label line
                       "    Drag 0.3\r\n"
                       "  }\r\n"
                       "  AsphaltRule asBirthRule :075acee0 {\r\n"
                       "    Position 1 2 3\r\n"
                       "    Mass 0.1\r\n"
                       "  }\r\n"
                       "  Count 7\r\n" // after the class block has ended: never read
                       "}\r\n";
    std::string err;
    auto f = data::parseDat(text, testSchema(), &err);
    ASSERT_TRUE(f) << err;
    const auto* top = f->top();
    ASSERT_TRUE(top);
    EXPECT_FLOAT_EQ(*top->getFloat("Mass"), 0.1f);
    ASSERT_TRUE(top->child("Aero"));
    EXPECT_FLOAT_EQ(*top->child("Aero")->getFloat("Drag"), 0.3f);
    EXPECT_FALSE(top->child("Count"));
    EXPECT_FALSE(top->child("AsphaltRule"));
    EXPECT_FALSE(top->child("Position"));

    // Without the schema every labelled block is a block.
    auto tree = data::parseDat(text, &err);
    ASSERT_TRUE(tree) << err;
    EXPECT_FLOAT_EQ(*tree->top()->getFloat("Mass"), 4500.0f);
    EXPECT_EQ(*tree->top()->getInt("Count"), 7);
}

// An unknown name followed by '{' skips the whole block.
TEST(ParityDatFile, UnknownNameBeforeABraceSkipsTheBlock) {
    auto f = data::parseDat("type: a\nc {\n Rule\n {\n Mass 1\n { Mass 2 }\n }\n Count 3\n}\n", testSchema());
    ASSERT_TRUE(f);
    EXPECT_FALSE(f->top()->child("Mass"));
    EXPECT_EQ(*f->top()->getInt("Count"), 3);
}

// The token after an unknown name is read before the rest of the line is
// skipped, so number lists of unknown fields swallow lines in pairs, and the
// tokenizer's look-ahead character means a token that ends on a bare line
// feed takes the following line with it.
TEST(ParityDatFile, SkippingFollowsTheTokenizerLookAhead) {
    // CR LF: "GearRatios" skips the rest of the "1" line; "2" is unknown and
    // skips the rest of "Count 5"'s line; "Mass 3" is read.
    const char* text = "type: a\r\nc {\r\n GearRatios\r\n 1\r\n 2\r\n Count 5\r\n Mass 3\r\n}\r\n";
    auto crlf = data::parseDat(text, testSchema());
    ASSERT_TRUE(crlf);
    EXPECT_FALSE(crlf->top()->child("Count"));
    EXPECT_FLOAT_EQ(*crlf->top()->getFloat("Mass"), 3.0f);
    // LF only: "1" ends on the line feed, so skipping "the rest of its line"
    // skips the "Mass 2" line.
    auto lf = data::parseDat("type: a\nc {\n Old 1\n Mass 2\n Count 4\n}\n", testSchema());
    ASSERT_TRUE(lf);
    EXPECT_FALSE(lf->top()->child("Mass"));
    EXPECT_EQ(*lf->top()->getInt("Count"), 4);
}

TEST(ParityDatFile, RecordsTakeExactlyTheirTokens) {
    // A vector takes three tokens whatever they are; a non-number reads as 0.
    auto f = data::parseDat("type: a\r\nc {\r\n Box 1 2\r\n Mass 5 6\r\n Count 1.9\r\n}\r\n", testSchema());
    ASSERT_TRUE(f);
    const auto box = f->top()->getVec3("Box");
    ASSERT_TRUE(box);
    EXPECT_FLOAT_EQ(box->y, 2.0f);
    EXPECT_FLOAT_EQ(box->z, 0.0f); // "Mass" was taken as the third component
    EXPECT_FALSE(f->top()->child("Mass"));
    EXPECT_EQ(*f->top()->getInt("Count"), 1);
}
