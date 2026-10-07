#pragma once

// Menu widgets for the original 640x480 frontend.
//
// The original menus are full-screen JPEG backgrounds with the labels and
// empty boxes painted in; interactive elements are TGA sprite sheets with
// the states stacked vertically (see docs/frontend.md). Widgets here are
// positioned in that 640x480 space and drawn through render::Overlay2D.

#include "core/Math.h"
#include "platform/Input.h"
#include "render/Overlay2D.h"
#include "ui/Font.h"
#include "ui/Text.h"
#include "ui/TextureCache.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mm2::ui {

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

// Navigation intents, merged from keyboard, gamepad and mouse.
struct NavInput {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false;
    bool enter = false; // Return/keypad Enter only (text entry commits)
    bool tabNext = false, tabPrev = false;
    Vec2 mouse{-1e9f, -1e9f}; // virtual coordinates
    bool mouseMoved = false;
    bool mousePressed = false; // left button went down this frame
    bool mouseDown = false;
    float wheel = 0.0f;
    std::string text;     // typed text this frame (text entry widgets)
    bool backspace = false;
};

// Turns raw input into NavInput with key/stick auto-repeat (the platform
// layer reports only the initial key press).
class NavReader {
public:
    NavInput read(const platform::Input& input, const render::UiLayout& layout, double dt);

private:
    struct Repeat {
        bool held = false;
        double timer = 0.0;
    };
    bool repeat(Repeat& r, bool down, double dt);
    Repeat m_up, m_down, m_left, m_right, m_backspace;
    Vec2 m_lastMouse{-1e9f, -1e9f};
};

// Shared per-frame drawing environment.
struct UiFrame {
    render::Overlay2D& overlay;
    TextureCache& textures;
    TextRenderer& text;
    const NavInput& nav;
    double time = 0.0;
};

// Sprite sheet: `frames` equally tall states stacked vertically.
struct SpriteSheet {
    std::string path; // "texture/main_sp.tga"
    int frames = 4;
};

// Draws one state of a sprite sheet with its top-left at (x, y).
void drawSpriteFrame(UiFrame& f, const SpriteSheet& sheet, int frame, float x, float y,
                     std::uint32_t color = 0xFFFFFFFFu);
// Size of one frame of a sheet (0x0 if the texture is missing).
Vec2 spriteFrameSize(UiFrame& f, const SpriteSheet& sheet);

// Colours and fonts used for text drawn into the backgrounds' boxes. The
// original's exact choices are not known; these match the painted labels.
namespace style {
inline constexpr std::uint32_t kValueText = 0xFFFFFFFFu;          // white
inline constexpr std::uint32_t kValueTextFocus = 0xFF33E0FFu;     // yellow (0xAABBGGRR)
inline constexpr std::uint32_t kValueTextDisabled = 0xFF8C7C7Cu;  // grey-blue
inline constexpr std::uint32_t kListHighlight = 0x80FF8040u;      // translucent blue bar
inline constexpr std::uint32_t kHelpText = 0xFF00FFFFu;           // yellow, like the help pictures
FontSpec valueFont();   // "Arial Bold, 12, 16" (string 563)
FontSpec smallFont();   // "Arial Bold, 12, 14" (string 567)
FontSpec titleFont();   // "Gill Sans MT, 16, 22, bold" (string 251)
} // namespace style

class Widget {
public:
    virtual ~Widget() = default;

    Box box;
    bool visible = true;
    bool enabled = true;
    std::string help; // help picture shown while focused ("jpg/mn_sp.jpg")

    virtual bool focusable() const { return visible && enabled; }
    virtual void draw(UiFrame& f, bool focused) = 0;
    // Enter / A / click. Returns true if handled.
    virtual bool activate(UiFrame&) { return false; }
    // Left/right. Returns true if handled (otherwise focus may move).
    virtual bool adjust(UiFrame&, int /*dir*/) { return false; }
    // Raw mouse handling for drag-style widgets; called every frame while
    // the widget is focused or captured.
    virtual void mouse(UiFrame&, bool /*hovered*/) {}
    // True while the widget wants all input (open dropdown, text entry).
    virtual bool modal() const { return false; }
    // Input routed to a modal widget.
    virtual void modalInput(UiFrame&) {}
    // Drawn after all widgets (drop-down lists).
    virtual void drawPopup(UiFrame&) {}
};

// Arrow/box buttons: frames normal, highlighted, pressed, disabled (4) or
// normal, highlighted, disabled (3) or with a 5th frame (mnav_opt).
class SpriteButton : public Widget {
public:
    SpriteButton(SpriteSheet sheet, float x, float y, std::function<void()> onClick);
    void draw(UiFrame& f, bool focused) override;
    bool activate(UiFrame&) override;
    SpriteSheet sheet;
    std::function<void()> onClick;

private:
    double m_pressedUntil = 0.0;
};

// Rows with a lamp or check box (blitz.tga, aud_fx.tga ...): five frames
// off, off+highlight, on, on+highlight, disabled. Used as radio items (with
// a shared group) or toggles.
class LampItem : public Widget {
public:
    LampItem(SpriteSheet sheet, float x, float y, std::function<bool()> isOn, std::function<void()> onClick);
    void draw(UiFrame& f, bool focused) override;
    bool activate(UiFrame&) override;
    SpriteSheet sheet;
    std::function<bool()> isOn;
    std::function<void()> onClick;
};

// Text value drawn in one of the backgrounds' navy boxes. Left/right cycle
// through the options; Enter or a click opens a drop-down list.
class ValueBox : public Widget {
public:
    ValueBox(Box box, std::function<std::vector<std::string>()> options, std::function<int()> get,
             std::function<void(int)> set);
    void draw(UiFrame& f, bool focused) override;
    bool activate(UiFrame& f) override;
    bool adjust(UiFrame& f, int dir) override;
    bool modal() const override { return m_open; }
    void modalInput(UiFrame& f) override;
    void drawPopup(UiFrame& f) override;

    std::function<std::vector<std::string>()> options;
    std::function<int()> get;
    std::function<void(int)> set;
    bool wrap = false;

private:
    Box listBox(const std::vector<std::string>& opts) const;
    bool m_open = false;
    int m_hover = 0;
    int m_scroll = 0;
};

// Read-only text in a box (driver name, totals).
class TextBox : public Widget {
public:
    TextBox(Box box, std::function<std::string()> text, Align align = Align::Left);
    bool focusable() const override { return false; }
    void draw(UiFrame& f, bool focused) override;
    std::function<std::string()> text;
    Align align;
    FontSpec font = style::valueFont();
    std::uint32_t color = style::kValueText;
};

// Horizontal slider inside one row of a grid box: [<] value bar [>], using
// slider_larr/slider_rarr (or the balance arrows) and slider_actl /
// slider_inactl cropped to the value.
class Slider : public Widget {
public:
    Slider(Box row, std::function<float()> get, std::function<void(float)> set, float step = 0.1f);
    void draw(UiFrame& f, bool focused) override;
    bool adjust(UiFrame& f, int dir) override;
    void mouse(UiFrame& f, bool hovered) override;
    std::function<float()> get; // 0..1
    std::function<void(float)> set;
    float step;
    bool balance = false; // use the balance arrows (slider_lbal/rbal)

private:
    bool m_dragging = false;
    double m_arrowPressedUntil = 0.0;
    int m_arrowPressed = 0;
};

// Selectable list drawn into a box (driver list, session list).
class ListBox : public Widget {
public:
    ListBox(Box box, std::function<std::vector<std::string>()> items, std::function<int()> get,
            std::function<void(int)> set);
    void draw(UiFrame& f, bool focused) override;
    bool adjust(UiFrame&, int) override { return false; }
    void mouse(UiFrame& f, bool hovered) override;
    // Up/down move the selection while focused; returns true if consumed.
    bool moveSelection(int dir);
    std::function<std::vector<std::string>()> items;
    std::function<int()> get;
    std::function<void(int)> set;
    std::function<void()> onDoubleClick;
    float rowHeight = 18.0f;

private:
    int m_scroll = 0;
    double m_lastClick = -1.0;
    int m_lastClickRow = -1;
};

// Single-line text entry (driver name, network name).
class TextEntry : public Widget {
public:
    TextEntry(Box box, std::string* value, std::size_t maxLength = 20);
    void draw(UiFrame& f, bool focused) override;
    bool activate(UiFrame&) override;
    bool modal() const override { return m_editing; }
    void modalInput(UiFrame& f) override;
    void beginEdit() { m_editing = true; }
    std::string* value;
    std::size_t maxLength;
    std::function<void()> onCommit;

private:
    bool m_editing = false;
};

// Static picture from the archives drawn into a box (map previews).
class Picture : public Widget {
public:
    Picture(Box box, std::function<std::string()> path);
    bool focusable() const override { return false; }
    void draw(UiFrame& f, bool focused) override;
    std::function<std::string()> path;
};

// Free-form drawing hook (performance bars, custom panels).
class Custom : public Widget {
public:
    explicit Custom(std::function<void(UiFrame&)> fn) : m_fn(std::move(fn)) {}
    bool focusable() const override { return false; }
    void draw(UiFrame& f, bool) override { m_fn(f); }

private:
    std::function<void(UiFrame&)> m_fn;
};

// One screen of the frontend: a background picture plus widgets, with focus
// handling. Spatial navigation picks the nearest focusable widget in the
// pressed direction.
class Menu {
public:
    explicit Menu(std::string bg = {}) : background(std::move(bg)) {}

    template <class W, class... Args>
    W& add(Args&&... args) {
        auto w = std::make_unique<W>(std::forward<Args>(args)...);
        W& ref = *w;
        m_widgets.push_back(std::move(w));
        return ref;
    }

    // Processes navigation for the focused widget.
    void update(UiFrame& f);
    // Background, then drawContent().
    void draw(UiFrame& f);
    // Widgets, the help picture and pop-ups (no background).
    void drawContent(UiFrame& f);

    std::string background;      // full-screen picture ("jpg/main_bk.jpg")
    Box helpBox{39, 394, 219, 69}; // where help pictures go (bottom-left black box)
    std::string defaultHelp;
    std::function<void()> onBack; // Esc / B / right click

    Widget* focused() const;
    void focus(const Widget* w);
    bool modalActive() const;

private:
    void moveFocus(Vec2 dir);
    std::vector<std::unique_ptr<Widget>> m_widgets;
    int m_focus = -1;
};

} // namespace mm2::ui
