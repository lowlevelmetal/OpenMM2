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

TEST(Menu, InitialFocusSkipsNavigationStrip) {
    Fixture fx;
    ui::Menu m;
    int a = 0, b = 0;
    addBox(m, 439, 1, &a, 2);   // in the top strip
    auto& content = addBox(m, 400, 62, &b, 2);
    auto f = fx.frame();
    m.update(f);
    EXPECT_EQ(m.focused(), &content);
}

TEST(Menu, SpatialNavigationAndAdjust) {
    Fixture fx;
    ui::Menu m;
    int top = 0, bottom = 0, right = 0;
    auto& t = addBox(m, 100, 100, &top, 3);
    auto& bo = addBox(m, 100, 200, &bottom, 3);
    auto& r = addBox(m, 400, 210, &right, 3);
    auto f = fx.frame();
    m.update(f);
    ASSERT_EQ(m.focused(), &t);

    fx.nav = {};
    fx.nav.down = true;
    m.update(f);
    EXPECT_EQ(m.focused(), &bo);

    // Right on a value box changes the value instead of moving focus.
    fx.nav = {};
    fx.nav.right = true;
    m.update(f);
    EXPECT_EQ(bottom, 1);
    EXPECT_EQ(m.focused(), &bo);

    // Values clamp at the end of the list (no wrap by default).
    bottom = 2;
    m.update(f);
    EXPECT_EQ(bottom, 2);

    // Tab moves focus in widget order.
    fx.nav = {};
    fx.nav.tabNext = true;
    m.update(f);
    EXPECT_EQ(m.focused(), &r);
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
    auto f = fx.frame();
    m.update(f); // focus
    fx.nav = {};
    fx.nav.accept = true;
    m.update(f);
    ASSERT_TRUE(box.modal());
    fx.nav = {};
    fx.nav.down = true;
    m.update(f);
    m.update(f);
    fx.nav = {};
    fx.nav.accept = true;
    m.update(f);
    EXPECT_FALSE(box.modal());
    EXPECT_EQ(v, 2);
}

TEST(TextEntry, TypingAndBackspace) {
    Fixture fx;
    ui::Menu m;
    std::string name = "Ab";
    auto& entry = m.add<ui::TextEntry>(ui::Box{10, 10, 200, 26}, &name, 5);
    entry.beginEdit();
    auto f = fx.frame();
    m.update(f); // focus
    fx.nav.text = "cdefg";
    m.update(f);
    EXPECT_EQ(name, "Abcde"); // max length 5
    fx.nav = {};
    fx.nav.backspace = true;
    m.update(f);
    EXPECT_EQ(name, "Abcd");
    fx.nav = {};
    fx.nav.enter = true;
    m.update(f);
    EXPECT_FALSE(entry.modal());
}
