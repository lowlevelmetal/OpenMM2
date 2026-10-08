// Frontend widget behaviour checked against MM2's own code (MM2Recomp,
// midtown2.exe build 3393); see docs/parity/frontend-ui.md.
#include "render/Device.h"
#include "ui/Widgets.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace mm2;

namespace {

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
    render::BufferSlice uploadTransient(render::BufferKind kind, std::span<const std::byte> data) override {
        if (kind == render::BufferKind::Vertex) {
            m_vertices.resize(data.size() / sizeof(render::Vertex2D));
            std::memcpy(m_vertices.data(), data.data(), m_vertices.size() * sizeof(render::Vertex2D));
        }
        return {{1}, 0};
    }
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
    // Collects the colours of untextured overlay quads (rectangles).
    void draw(const render::DrawCall& call) override {
        if (!call.textures[0].texture)
            for (const auto& v : m_vertices)
                untexturedColors.push_back(v.color);
    }
    void requestCapture() override {}
    bool readCapture(render::Image&) override { return false; }
    void waitIdle() override {}
    const render::FrameStats& stats() const override { return m_stats; }

    std::vector<std::uint32_t> untexturedColors;

private:
    render::DeviceInfo m_info;
    render::FrameStats m_stats;
    std::uint32_t m_next = 0;
    std::vector<render::Vertex2D> m_vertices;
};

struct Fixture {
    NullDevice device;
    vfs::Vfs vfs;
    render::Overlay2D overlay{device};
    ui::TextureCache textures{device, vfs};
    ui::TextRenderer text{device};
    ui::NavInput nav;
    std::vector<std::string> sounds;
    ui::SoundFn soundFn = [this](std::string_view name, float) { sounds.emplace_back(name); };
    ui::UiFrame frame() { return ui::UiFrame{overlay, textures, text, nav, 0.0, &soundFn}; }
};

ui::ValueBox& addBox(ui::Menu& m, ui::Box b, int* value, int count) {
    return m.add<ui::ValueBox>(
        b,
        [count] {
            std::vector<std::string> v;
            for (int i = 0; i < count; ++i)
                v.push_back(std::to_string(i));
            return v;
        },
        [value] { return *value; }, [value](int i) { *value = i; });
}

} // namespace

// TextDropWidget::IncDrop / DecDrop step onto any entry; TextDropWidget::
// SetValue turns one that cannot be picked into the first that can.
TEST(FrontendParity, DropDownStepOntoLockedEntryJumpsToFirstOpen) {
    Fixture fx;
    ui::Menu m;
    int v = 2;
    auto& box = addBox(m, {404, 66, 205, 24}, &v, 6);
    box.optionEnabled = [](int i) { return i <= 2; }; // checkpoint races: groups of three
    auto f = fx.frame();
    ASSERT_TRUE(box.activate(f));
    fx.nav = {};
    fx.nav.down = true; // 2 -> 3, locked -> 0
    box.modalInput(f);
    fx.nav = {};
    fx.nav.accept = true;
    box.modalInput(f);
    EXPECT_FALSE(box.modal());
    EXPECT_EQ(v, 0);
}

// UITextDropdown::CaptureAction: a release over a locked entry picks the
// first open one; mmDropDown::InitString: a list too tall for the screen
// whose second column would not fit right of the box starts one box width
// further left per extra column.
TEST(FrontendParity, DropDownColumnsAndLockedClick) {
    Fixture fx;
    ui::Menu m;
    int v = 0;
    // The garage's VEHICLES box: rows from y 300, 7 per column; 10 entries
    // need a second column, which starts at 404 after the list moved left.
    auto& box = addBox(m, {404, 277, 205, 19}, &v, 10);
    auto f = fx.frame();
    ASSERT_TRUE(box.activate(f));
    fx.nav = {};
    fx.nav.mouse = {410, 305};
    fx.nav.mouseReleased = true;
    box.modalInput(f);
    EXPECT_EQ(v, 7);

    box.optionEnabled = [](int i) { return i != 8; };
    v = 3;
    ASSERT_TRUE(box.activate(f));
    fx.nav = {};
    fx.nav.mouse = {410, 305 + 23}; // entry 8, locked
    fx.nav.mouseReleased = true;
    box.modalInput(f);
    EXPECT_FALSE(box.modal());
    EXPECT_EQ(v, 0);
}

// MenuManager::ScanGlobalKeys: Escape with the focus on the navigation strip
// moves it back to the page and backs the page up.
TEST(FrontendParity, EscapeOnTheStripBacksThePageUp) {
    Fixture fx;
    ui::Menu m;
    int a = 0, b = 0;
    auto& page = addBox(m, {400, 62, 205, 24}, &a, 2);
    auto& strip = addBox(m, {439, 1, 100, 24}, &b, 2);
    strip.group = 1;
    bool back = false;
    m.onBack = [&] { back = true; };
    m.focus(&strip);
    fx.nav.back = true;
    auto f = fx.frame();
    m.update(f);
    EXPECT_TRUE(back);
    EXPECT_EQ(m.focused(), &page);
}

// UIBMButton::Action plays the button's sound on the press; the menu acts on
// a release over the button (UIMenu::CheckMouseHits), wherever the press was.
TEST(FrontendParity, SpriteButtonSoundOnPressActionOnRelease) {
    Fixture fx;
    ui::Menu m;
    int clicks = 0;
    auto& button = m.add<ui::SpriteButton>(ui::SpriteSheet{"texture/none.tga", 4}, 100, 100, [&] { ++clicks; });
    button.box.w = button.box.h = 30;
    button.sound = "Selectionmade";
    auto f = fx.frame();
    fx.nav.mouse = {110, 110};
    fx.nav.mouseMoved = fx.nav.mousePressed = fx.nav.mouseDown = true;
    m.update(f);
    EXPECT_EQ(fx.sounds, std::vector<std::string>{"Selectionmade"});
    EXPECT_EQ(clicks, 0);
    fx.nav = {};
    fx.nav.mouse = {110, 110};
    fx.nav.mouseReleased = true;
    m.update(f);
    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(fx.sounds.size(), 1u);
    // A release over the button after a press elsewhere acts too.
    fx.nav = {};
    fx.nav.mouse = {110, 110};
    fx.nav.mouseMoved = true;
    fx.nav.mouseReleased = true;
    m.update(f);
    EXPECT_EQ(clicks, 2);
}

// A shown UIIcon (the race map) is a focus stop without a highlight; a
// hidden one is not.
TEST(FrontendParity, ShownIconIsAFocusStop) {
    Fixture fx;
    ui::Menu m;
    int a = 0;
    addBox(m, {404, 66, 205, 24}, &a, 2);
    std::string path = "jpg/sf_maproam.jpg";
    auto& map = m.add<ui::Picture>(ui::Box{22, 194, 242, 184}, [&] { return path; });
    map.focusStop = true;
    m.setInitialFocus(&map);
    EXPECT_EQ(m.focused(), &map);
    path.clear();
    EXPECT_FALSE(map.focusable());
    m.resetFocus();
    EXPECT_NE(m.focused(), &map);
}

// UITextField::ToggleField: the field has no frame of its own (it is painted
// on the background); only while editing is the text on an opaque black card.
TEST(FrontendParity, TextFieldHasNoFrameAndABlackCardWhileEditing) {
    Fixture fx;
    ui::Menu m;
    std::string name = "Bob";
    auto& entry = m.add<ui::TextEntry>(ui::Box{72, 89, 200, 20}, &name, 18);
    auto rectangles = [&](bool focused) {
        fx.device.untexturedColors.clear();
        auto f = fx.frame();
        fx.overlay.begin(render::UiScaleMode::Fit);
        entry.draw(f, focused);
        fx.overlay.end();
        return fx.device.untexturedColors;
    };
    EXPECT_TRUE(rectangles(false).empty());
    EXPECT_TRUE(rectangles(true).empty()); // focused, not editing
    entry.beginEdit();
    const auto card = rectangles(true);
    ASSERT_EQ(card.size(), 4u);
    for (const auto c : card)
        EXPECT_EQ(c, render::packColor(0, 0, 0));
}
