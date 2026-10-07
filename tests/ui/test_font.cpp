#include "ui/Font.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(FontSpec, ParsesStringTableEntries) {
    auto s = ui::FontSpec::parse("Gill Sans MT, 12, 24, 0, 400");
    ASSERT_TRUE(s);
    EXPECT_EQ(s->face, "Gill Sans MT");
    EXPECT_EQ(s->size2, 24);
    EXPECT_FALSE(s->bold());
    EXPECT_TRUE(ui::FontSpec::parse("Arial Bold, 32, 64, 0, 400")->bold());
    EXPECT_TRUE(ui::FontSpec::parse("Gill Sans MT, 16, 22, 0, 700")->bold());
    EXPECT_FALSE(ui::FontSpec::parse("Hello"));
}

TEST(Font, BundledFontsBakeAndMeasure) {
    for (auto [face, bold] : {std::pair{"Gill Sans MT", false}, std::pair{"Arial Bold", true}}) {
        auto font = ui::findFont(face, bold);
        ASSERT_TRUE(font) << face;
        auto atlas = ui::FontAtlas::bake(font, 24.0f);
        ASSERT_TRUE(atlas);
        EXPECT_GT(atlas->width(), 0);
        ASSERT_TRUE(atlas->glyph(U'A'));
        EXPECT_GT(atlas->glyph(U'A')->advance, 5.0f);
        EXPECT_NEAR(atlas->ascent() - atlas->descent(), 24.0f, 0.5f);
        const float w = atlas->measure("Midtown Madness");
        EXPECT_GT(w, 100.0f);
        EXPECT_LT(w, 300.0f);
        EXPECT_GT(atlas->measure("\xC3\xA9"), 0.0f); // e-acute (Latin-1 coverage)
    }
}

TEST(Font, WrapHonoursLiteralNewlines) {
    auto atlas = ui::FontAtlas::bake(ui::findFont("Gill Sans MT", false), 16.0f);
    ASSERT_TRUE(atlas);
    auto lines = atlas->wrap("You have not maintained \\n the minimum speed!", 1000.0f);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "You have not maintained");
    auto narrow = atlas->wrap("one two three four five six", 60.0f);
    EXPECT_GT(narrow.size(), 2u);
}
