#include "core/StringUtil.h"

#include <gtest/gtest.h>

using namespace mm2;

TEST(StringUtil, NormalizeVirtualPath) {
    EXPECT_EQ(str::normalizeVirtualPath("GAME\\MM2CORE.AR"), "game/mm2core.ar");
    EXPECT_EQ(str::normalizeVirtualPath("/tune//a/./b/../c.csv/"), "tune/a/c.csv");
    EXPECT_EQ(str::normalizeVirtualPath(".."), "");
}

TEST(StringUtil, ParseNumbers) {
    EXPECT_EQ(str::parseInt(" 42 "), 42);
    EXPECT_EQ(str::parseInt("-0x10"), -16);
    EXPECT_FALSE(str::parseInt("4x2"));
    EXPECT_DOUBLE_EQ(*str::parseDouble("+1.5"), 1.5);
    EXPECT_EQ(str::parseBool("Yes"), true);
}
