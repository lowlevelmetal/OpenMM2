#include "render/Device.h"
#include "ui/Widgets.h"

#include <gtest/gtest.h>

using namespace mm2;

namespace {

// A device that records nothing; enough for widget logic.
class NullDevice final : public render::Device {
public:
    const render::DeviceInfo& info() const override { return m_info; }
    render::TextureHandle createTexture(const render::TextureDesc&, std::span<const render::TextureData>) override {
        return {++m_next};
    }
    void updateTexture(render::TextureHandle, std::uint32_t, const render::Rect&, const void*, std::uint32_t) override {}
    void destroyTexture(render::TextureHandle) override {}
    render::BufferHandle createBuffer(render::BufferKind, std::size_t, const void*) override { return {++m_next}; }
    void updateBuffer(render::BufferHandle, std::size_t, std::span<const std::byte>) override {}
    void destroyBuffer(render::BufferHandle) override {}
    render::BufferSlice uploadTransient(render::BufferKind, std::span<const std::byte>) override { return {{1}, 0}; }
    void applySettings(const render::DisplaySettings&) override {}
    void notifyResized() override {}
    bool beginFrame() override { return true; }
    render::Extent2D outputExtent() const override { return {640, 480}; }
    render::Extent2D sceneExtent() const override { return {640, 480}; }
    void beginScene(const render::ClearValues&) override {}
    void endScene() override {}
    void beginOverlay(const Vec4&) override {}
    void endOverlay() override {}
    void endFrame() override {}
    void setViewport(const render::Viewport&) override {}
    void setScissor(const render::Rect*) override {}
    void clear(const render::ClearValues&) override {}
    void setFrameConstants(const render::FrameConstants&) override {}
    void draw(const render::DrawCall&) override {}
    void requestCapture() override {}
    bool readCapture(render::Image&) override { return false; }
    void waitIdle() override {}
    const render::FrameStats& stats() const override { return m_stats; }

private:
    render::DeviceInfo m_info;
    render::FrameStats m_stats;
    std::uint32_t m_next = 0;
};

struct Fixture {
    NullDevice device;
    vfs::Vfs vfs;
    render::Overlay2D overlay{device};
    ui::TextureCache textures{device, vfs};
    ui::TextRenderer text{device};
    ui::NavInput nav;
    ui::UiFrame frame() { return ui::UiFrame{overlay, textures, text, nav, 0.0}; }
};


ui::ValueBox& addBox(ui::Menu& m, float x, float y, int* value, int count) {
    return m.add<ui::ValueBox>(
        ui::Box{x, y, 100, 28},
        [count] {
            std::vector<std::string> v;
            for (int i = 0; i < count; ++i)
                v.push_back(std::to_string(i));
            return v;
        },
        [value] { return *value; }, [value](int i) { *value = i; });
}

} // namespace

TEST(Menu, InitialFocusIsTheFirstPageWidget) {
    Fixture fx;
    ui::Menu m;
    int a = 0, b = 0;
    auto& strip = addBox(m, 439, 1, &a, 2);
    strip.group = 1; // navigation strip
    auto& content = addBox(m, 400, 62, &b, 2);
    auto f = fx.frame();
    m.update(f);
    EXPECT_EQ(m.focused(), &content);
}

TEST(Menu, FocusFollowsCreationOrderAndCrossesToTheStrip) {
    Fixture fx;
    ui::Menu m;
    int v[4] = {};
    auto& first = addBox(m, 400, 300, &v[0], 3); // creation order, not position
    auto& second = addBox(m, 100, 100, &v[1], 3);
    auto& nav1 = addBox(m, 439, 1, &v[2], 3);
    auto& nav2 = addBox(m, 540, 1, &v[3], 3);
    nav1.group = nav2.group = 1;
    auto f = fx.frame();
    m.update(f);
    ASSERT_EQ(m.focused(), &first);

    auto press = [&](auto setter) {
        fx.nav = {};
        setter(fx.nav);
        m.update(f);
    };
    press([](ui::NavInput& n) { n.down = true; });
    EXPECT_EQ(m.focused(), &second);
    // Past the page's last widget: the strip's first.
    press([](ui::NavInput& n) { n.tabNext = true; });
    EXPECT_EQ(m.focused(), &nav1);
    press([](ui::NavInput& n) { n.down = true; });
    EXPECT_EQ(m.focused(), &nav2);
    // Past the strip's last: the page's first.
    press([](ui::NavInput& n) { n.down = true; });
    EXPECT_EQ(m.focused(), &first);
    // Up before the first: the other group's first (MenuManager::ToggleFocus).
    press([](ui::NavInput& n) { n.up = true; });
    EXPECT_EQ(m.focused(), &nav1);
    // Escape on the strip returns the focus to the page (and backs it up).
    press([](ui::NavInput& n) { n.back = true; });
    EXPECT_EQ(m.focused(), &first);
    // Left/Right never move focus, and do nothing on a closed drop-down.
    press([](ui::NavInput& n) { n.right = true; });
    EXPECT_EQ(m.focused(), &first);
    EXPECT_EQ(v[0], 0);
}

TEST(Menu, ResetFocusReturnsToTheInitialWidget) {
    Fixture fx;
    ui::Menu m;
    int a = 0, b = 0;
    addBox(m, 10, 10, &a, 2);
    auto& chosen = addBox(m, 10, 50, &b, 2);
    m.setInitialFocus(&chosen);
    auto f = fx.frame();
    fx.nav.up = true;
    m.update(f);
    EXPECT_NE(m.focused(), &chosen);
    m.resetFocus();
    EXPECT_EQ(m.focused(), &chosen);
}

TEST(Menu, BackCallsHandler) {
    Fixture fx;
    ui::Menu m;
    int v = 0;
    addBox(m, 100, 100, &v, 2);
    bool back = false;
    m.onBack = [&] { back = true; };
    fx.nav.back = true;
    auto f = fx.frame();
    m.update(f);
    EXPECT_TRUE(back);
}

TEST(ValueBox, DropDownSelectsWithKeyboard) {
    Fixture fx;
    ui::Menu m;
    int v = 0;
    auto& box = addBox(m, 100, 100, &v, 5);
    box.optionEnabled = [](int i) { return i != 1; };
    EXPECT_FLOAT_EQ(box.box.h, 23.0f); // UITextDropdown: drop_arrow height + 2
    auto f = fx.frame();
    m.update(f); // focus
    fx.nav = {};
    fx.nav.accept = true;
    m.update(f);
    ASSERT_TRUE(box.modal());
    fx.nav = {};
    fx.nav.end = true;
    m.update(f);
    fx.nav = {};
    fx.nav.up = true;
    m.update(f);
    fx.nav = {};
    fx.nav.up = true;
    m.update(f); // 4 -> 3 -> 2
    fx.nav = {};
    fx.nav.accept = true;
    m.update(f);
    EXPECT_FALSE(box.modal());
    EXPECT_EQ(v, 2);
    // Escape closes without picking.
    fx.nav = {};
    fx.nav.accept = true;
    m.update(f);
    fx.nav = {};
    fx.nav.right = true;
    m.update(f);
    fx.nav = {};
    fx.nav.back = true;
    m.update(f);
    EXPECT_FALSE(box.modal());
    EXPECT_EQ(v, 2);
}

TEST(ValueBox, RollerButtonsStepOverLockedEntries) {
    Fixture fx;
    ui::Menu m;
    int v = 0;
    auto& box = addBox(m, 100, 100, &v, 4);
    box.optionEnabled = [](int i) { return i != 2; };
    EXPECT_TRUE(ui::stepOption(box, 1, false));
    EXPECT_EQ(v, 1);
    EXPECT_FALSE(ui::stepOption(box, 1, false)); // clamping arrows stop at a locked entry
    EXPECT_EQ(v, 1);
    v = 3;
    EXPECT_FALSE(ui::stepOption(box, 1, false));
    EXPECT_TRUE(ui::stepOption(box, 1, true)); // wrapping arrows go round
    EXPECT_EQ(v, 0);
    EXPECT_TRUE(ui::stepOption(box, -1, true));
    EXPECT_EQ(v, 3);
}

TEST(Roller, StepsAndClamps) {
    Fixture fx;
    ui::Menu m;
    int v = 1;
    auto& r = m.add<ui::Roller>(
        ui::Box{418, 98, 60, 32}, [] { return std::vector<std::string>{"1", "2", "3", "4"}; }, [&] { return v; },
        [&](int i) { v = i; });
    r.maxIndex = 2;
    EXPECT_FLOAT_EQ(r.box.h, 34.0f);
    auto f = fx.frame();
    m.update(f);
    fx.nav.right = true;
    m.update(f);
    m.update(f);
    EXPECT_EQ(v, 2); // capped at maxIndex
    fx.nav = {};
    fx.nav.left = true;
    for (int i = 0; i < 4; ++i)
        m.update(f);
    EXPECT_EQ(v, 0);
}

TEST(Slider, TwentyPositionsOnTwoPixelSegments) {
    float value = 0.0f;
    ui::Slider s(ui::Box{450, 212, 183, 29}, [&] { return value; }, [&](float v) { value = v; });
    EXPECT_EQ(s.segments(), 66); // 132 px track between 25 px arrows
    EXPECT_FLOAT_EQ(s.step(), 1.0f / 19.0f);
    ui::Slider far(ui::Box{450, 179, 184, 29}, [&] { return value; }, [&](float v) { value = v; }, 100.0f, 1000.0f);
    EXPECT_NEAR(far.step(), 900.0f / 19.0f, 1e-3f);
    Fixture fx;
    auto f = fx.frame();
    s.adjust(f, 1);
    EXPECT_FLOAT_EQ(value, 1.0f / 19.0f);
    s.adjust(f, -1);
    s.adjust(f, -1);
    EXPECT_FLOAT_EQ(value, 0.0f);
}

TEST(TextEntry, FirstKeyReplacesTheText) {
    Fixture fx;
    ui::Menu m;
    std::string name = "Ab";
    auto& entry = m.add<ui::TextEntry>(ui::Box{10, 10, 200, 26}, &name, 5);
    bool committed = false;
    entry.onCommit = [&] { committed = true; };
    auto f = fx.frame();
    m.update(f); // focus = editing
    ASSERT_TRUE(entry.modal());
    fx.nav.text = "cdefgh";
    m.update(f);
    EXPECT_EQ(name, "cdefg"); // replaced, max length 5
    fx.nav = {};
    fx.nav.backspace = true;
    m.update(f);
    EXPECT_EQ(name, "cdef");
    fx.nav = {};
    fx.nav.enter = true;
    m.update(f);
    EXPECT_FALSE(entry.modal());
    EXPECT_TRUE(committed);
}

TEST(TextEntry, ClickOnAnotherWidgetTakesTheFocus) {
    Fixture fx;
    ui::Menu m;
    std::string name;
    auto& entry = m.add<ui::TextEntry>(ui::Box{10, 10, 200, 26}, &name, 18);
    bool clicked = false;
    auto& button = m.add<ui::SpriteButton>(ui::SpriteSheet{"texture/none.tga", 4}, 10, 100, [&] { clicked = true; });
    button.box.w = button.box.h = 30; // no texture in the test: give it a size
    auto f = fx.frame();
    m.update(f);
    ASSERT_EQ(m.focused(), &entry);
    fx.nav = {};
    fx.nav.mouse = {20, 110};
    fx.nav.mouseMoved = fx.nav.mousePressed = fx.nav.mouseDown = true;
    m.update(f);
    EXPECT_EQ(m.focused(), &button);
    EXPECT_FALSE(entry.modal());
    fx.nav = {};
    fx.nav.mouse = {20, 110};
    fx.nav.mouseReleased = true;
    m.update(f);
    EXPECT_TRUE(clicked);
}
