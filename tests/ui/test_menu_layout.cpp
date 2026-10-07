#include "TestData.h"
#include "ui/MenuLayout.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

constexpr const char* kWidgets = "MENU NAME,ID,WIDGET NAME,ID,X,Y,W,H,DESC\r\n"
                                 "Main Menu,1,dvrcc,0,439,242,230,90,4 State crash\r\n"
                                 "Main Menu,1,DRIVER NAME,4,177,128,205,24,\r\n"
                                 "Audio Options,3,opt_can,1,290,415,90,4 State,\r\n"
                                 "quit_dlg,27,popup_ok,0,296,38,79,34,4 State\r\n"
                                 "Crash Course Intro,40,cci_lon,0,439,359,201,56,4 State\r\n"
                                 "Crash Course Intro,40,2,mnav_prev,4,1,290,415,90\r\n";

} // namespace

TEST(MenuLayout, ReplacesNonZeroValuesLikeTheGame) {
    ui::MenuLayout l;
    l.parseWidgets(kWidgets);
    // Sprite buttons pass only a position: width and height stay zero.
    const ui::Box b = l.widget(1, 0, {50, 120, 0, 0});
    EXPECT_FLOAT_EQ(b.x, 439);
    EXPECT_FLOAT_EQ(b.y, 242);
    EXPECT_FLOAT_EQ(b.w, 0);
    EXPECT_FLOAT_EQ(b.h, 0);
    // Text widgets pass a size, which the table replaces too.
    const ui::Box d = l.widget(1, 4, {50, 32, 240, 32});
    EXPECT_FLOAT_EQ(d.x, 177);
    EXPECT_FLOAT_EQ(d.w, 205);
    EXPECT_FLOAT_EQ(d.h, 24);
    // Unlisted widgets keep the code's rectangle.
    const ui::Box u = l.widget(1, 9, {10, 20, 30, 40});
    EXPECT_FLOAT_EQ(u.x, 10);
    EXPECT_FLOAT_EQ(u.h, 40);
}

TEST(MenuLayout, ParsesLikeStrtokAndAtoi) {
    ui::MenuLayout l;
    l.parseWidgets(kWidgets);
    // "90,4 State," -> W 90, H 4.
    const auto* r = l.find(3, 1);
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->box.w, 90);
    EXPECT_FLOAT_EQ(r->box.h, 4);
    // The malformed row reads as widget 0 of menu 40, but the first row wins.
    EXPECT_FLOAT_EQ(l.position(40, 0, {1, 1}).x, 439);
    EXPECT_EQ(l.widgets().size(), 6u);
}

TEST(MenuLayout, DialogWidgetsAreRelative) {
    ui::MenuLayout l;
    l.parseWidgets(kWidgets);
    const Vec2 origin = ui::dialogOrigin({400, 76});
    EXPECT_FLOAT_EQ(origin.x, 120);
    EXPECT_FLOAT_EQ(origin.y, 202);
    const Vec2 p = l.position(27, 0, {1, 1}, origin);
    EXPECT_FLOAT_EQ(p.x, 416);
    EXPECT_FLOAT_EQ(p.y, 240);
}

TEST(MenuLayout, RetailTables) {
    MM2_REQUIRE_GAME_DATA();
    const auto l = ui::MenuLayout::load(*test::gameData());
    EXPECT_GT(l.widgets().size(), 250u);
    // Main Menu: the right-hand column and the driver box.
    EXPECT_FLOAT_EQ(l.position(1, 3, {1, 1}).y, 415);
    EXPECT_FLOAT_EQ(l.widget(1, 4, {1, 1, 1, 1}).w, 205);
    // Single Race Menu map.
    EXPECT_FLOAT_EQ(l.widget(7, 22, {1, 1, 1, 1}).x, 22);
}
