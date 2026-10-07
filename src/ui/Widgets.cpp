#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

namespace mm2::ui {
namespace {

constexpr double kRepeatDelay = 0.40;
constexpr double kRepeatRate = 0.075;
constexpr float kStickThreshold = 0.5f;

std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return render::packColor(r, g, b, a);
}

} // namespace

// --- Style -----------------------------------------------------------------------

namespace style {
FontSpec valueFont() { return {"Arial Bold", 12, 16, 0, 400}; }
FontSpec smallFont() { return {"Arial Bold", 12, 14, 0, 400}; }
FontSpec titleFont() { return {"Gill Sans MT", 16, 22, 0, 700}; }
} // namespace style

// --- Input -----------------------------------------------------------------------

bool NavReader::repeat(Repeat& r, bool down, double dt) {
    if (!down) {
        r.held = false;
        return false;
    }
    if (!r.held) {
        r.held = true;
        r.timer = kRepeatDelay;
        return true;
    }
    r.timer -= dt;
    if (r.timer <= 0.0) {
        r.timer += kRepeatRate;
        return true;
    }
    return false;
}

NavInput NavReader::read(const platform::Input& in, const render::UiLayout& layout, double dt) {
    using platform::GamepadAxis;
    using platform::GamepadButton;
    using platform::Key;
    NavInput n;

    bool up = in.keyDown(Key::Up) || in.keyDown(Key::Kp8);
    bool down = in.keyDown(Key::Down) || in.keyDown(Key::Kp2);
    bool left = in.keyDown(Key::Left) || in.keyDown(Key::Kp4);
    bool right = in.keyDown(Key::Right) || in.keyDown(Key::Kp6);
    n.accept = in.keyPressed(Key::Return) || in.keyPressed(Key::KpEnter) || in.keyPressed(Key::Space);
    n.back = in.keyPressed(Key::Escape);
    n.enter = in.keyPressed(Key::Return) || in.keyPressed(Key::KpEnter);
    const bool shift = in.keyDown(Key::LShift) || in.keyDown(Key::RShift);
    if (in.keyPressed(Key::Tab)) {
        n.tabNext = !shift;
        n.tabPrev = shift;
    }
    for (const auto& pad : in.gamepads()) {
        auto btn = [&](GamepadButton b) { return pad.buttons.test(static_cast<int>(b)); };
        auto hit = [&](GamepadButton b) { return pad.pressed.test(static_cast<int>(b)); };
        const float lx = pad.axes[static_cast<int>(GamepadAxis::LeftX)];
        const float ly = pad.axes[static_cast<int>(GamepadAxis::LeftY)];
        up |= btn(GamepadButton::DpadUp) || ly < -kStickThreshold;
        down |= btn(GamepadButton::DpadDown) || ly > kStickThreshold;
        left |= btn(GamepadButton::DpadLeft) || lx < -kStickThreshold;
        right |= btn(GamepadButton::DpadRight) || lx > kStickThreshold;
        n.accept |= hit(GamepadButton::South) || hit(GamepadButton::Start);
        n.back |= hit(GamepadButton::East) || hit(GamepadButton::Back);
        n.tabNext |= hit(GamepadButton::RightShoulder);
        n.tabPrev |= hit(GamepadButton::LeftShoulder);
    }
    n.up = repeat(m_up, up, dt);
    n.down = repeat(m_down, down, dt);
    n.left = repeat(m_left, left, dt);
    n.right = repeat(m_right, right, dt);
    n.backspace = repeat(m_backspace, in.keyDown(Key::Backspace), dt);

    n.mouse = layout.toVirtual(in.mousePosition());
    n.mouseMoved = n.mouse.x != m_lastMouse.x || n.mouse.y != m_lastMouse.y;
    m_lastMouse = n.mouse;
    n.mousePressed = in.mousePressed(platform::MouseButton::Left);
    n.mouseDown = in.mouseDown(platform::MouseButton::Left);
    n.back |= in.mousePressed(platform::MouseButton::Right);
    n.wheel = in.mouseWheel().y;
    n.text = in.text();
    return n;
}

// --- Sprites -----------------------------------------------------------------------

Vec2 spriteFrameSize(UiFrame& f, const SpriteSheet& sheet) {
    const UiTexture& t = f.textures.get(sheet.path);
    if (!t || sheet.frames <= 0)
        return {};
    return {static_cast<float>(t.width), static_cast<float>(t.height) / static_cast<float>(sheet.frames)};
}

void drawSpriteFrame(UiFrame& f, const SpriteSheet& sheet, int frame, float x, float y, std::uint32_t color) {
    const UiTexture& t = f.textures.get(sheet.path);
    if (!t || sheet.frames <= 0)
        return;
    frame = std::clamp(frame, 0, sheet.frames - 1);
    const float fh = static_cast<float>(t.height) / static_cast<float>(sheet.frames);
    // Game images are stored bottom row first: frame 0 (the top one) is at v = 1.
    const float vTop = 1.0f - static_cast<float>(frame) / static_cast<float>(sheet.frames);
    const float vBottom = 1.0f - static_cast<float>(frame + 1) / static_cast<float>(sheet.frames);
    f.overlay.image(t.handle, x, y, static_cast<float>(t.width), fh, {0.0f, vTop}, {1.0f, vBottom}, color);
}

// --- SpriteButton -------------------------------------------------------------------

SpriteButton::SpriteButton(SpriteSheet sheet_, float x, float y, std::function<void()> click)
    : sheet(std::move(sheet_)), onClick(std::move(click)) {
    box = {x, y, 0, 0};
}

void SpriteButton::draw(UiFrame& f, bool focused) {
    const Vec2 size = spriteFrameSize(f, sheet);
    box.w = size.x;
    box.h = size.y;
    int frame = 0;
    const bool pressed = f.time < m_pressedUntil || (focused && f.nav.mouseDown && box.contains(f.nav.mouse));
    if (!enabled)
        frame = sheet.frames >= 4 ? sheet.frames - 1 : 0;
    else if (pressed && sheet.frames >= 3)
        frame = 2;
    else if (focused)
        frame = 1;
    drawSpriteFrame(f, sheet, frame, box.x, box.y,
                    !enabled && sheet.frames < 4 ? render::packColor(120, 120, 120) : 0xFFFFFFFFu);
}

bool SpriteButton::activate(UiFrame& f) {
    if (!enabled)
        return false;
    m_pressedUntil = f.time + 0.12;
    if (onClick)
        onClick();
    return true;
}

// --- LampItem --------------------------------------------------------------------------

LampItem::LampItem(SpriteSheet sheet_, float x, float y, std::function<bool()> on, std::function<void()> click)
    : sheet(std::move(sheet_)), isOn(std::move(on)), onClick(std::move(click)) {
    box = {x, y, 0, 0};
}

void LampItem::draw(UiFrame& f, bool focused) {
    const Vec2 size = spriteFrameSize(f, sheet);
    box.w = size.x;
    box.h = size.y;
    const bool on = isOn && isOn();
    const int frame = !enabled ? 4 : (on ? (focused ? 3 : 2) : (focused ? 1 : 0));
    drawSpriteFrame(f, sheet, frame, box.x, box.y);
}

bool LampItem::activate(UiFrame&) {
    if (!enabled)
        return false;
    if (onClick)
        onClick();
    return true;
}

// --- ValueBox ---------------------------------------------------------------------------

ValueBox::ValueBox(Box b, std::function<std::vector<std::string>()> opts, std::function<int()> g,
                   std::function<void(int)> s)
    : options(std::move(opts)), get(std::move(g)), set(std::move(s)) {
    box = b;
}

void ValueBox::draw(UiFrame& f, bool focused) {
    const auto opts = options();
    const int cur = get ? get() : -1;
    const std::string text = cur >= 0 && cur < static_cast<int>(opts.size()) ? opts[static_cast<std::size_t>(cur)] : "";
    const std::uint32_t color =
        !enabled ? style::kValueTextDisabled : (focused ? style::kValueTextFocus : style::kValueText);
    const FontSpec font = style::valueFont();
    const float lh = f.text.lineHeight(f.overlay, font);
    const bool arrows = enabled && box.w >= 60 && opts.size() > 1;
    const float textRight = box.x + box.w - (arrows ? 24.0f : 6.0f);
    f.overlay.setClip(nullptr);
    const Vec4 clip{box.x + 2, box.y, textRight - box.x - 2, box.h};
    f.overlay.setClip(&clip);
    f.text.draw(f.overlay, font, text, box.x + 8, box.y + (box.h - lh) * 0.5f, color);
    f.overlay.setClip(nullptr);
    if (arrows)
        drawSpriteFrame(f, {"texture/drop_arrow.tga", 3}, m_open ? 2 : (focused ? 1 : 0), box.x + box.w - 22,
                        box.y + (box.h - 21.0f) * 0.5f);
}

Box ValueBox::listBox(const std::vector<std::string>& opts) const {
    const float row = 18.0f;
    const int rows = std::min<int>(static_cast<int>(opts.size()), 10);
    Box b{box.x, box.y + box.h, box.w, row * static_cast<float>(rows) + 4};
    if (b.y + b.h > 478)
        b.y = box.y - b.h;
    return b;
}

bool ValueBox::activate(UiFrame& f) {
    if (!enabled || options().size() < 2)
        return false;
    m_open = true;
    m_hover = std::max(0, get());
    m_scroll = std::max(0, m_hover - 4);
    (void)f;
    return true;
}

bool ValueBox::adjust(UiFrame&, int dir) {
    if (!enabled)
        return false;
    const int n = static_cast<int>(options().size());
    if (n == 0)
        return true;
    int v = get() + dir;
    if (wrap)
        v = (v % n + n) % n;
    v = std::clamp(v, 0, n - 1);
    if (v != get())
        set(v);
    return true;
}

void ValueBox::modalInput(UiFrame& f) {
    const auto opts = options();
    const int n = static_cast<int>(opts.size());
    const Box lb = listBox(opts);
    const int rows = std::min(n, 10);
    const NavInput& nav = f.nav;
    if (nav.up)
        m_hover = std::max(0, m_hover - 1);
    if (nav.down)
        m_hover = std::min(n - 1, m_hover + 1);
    if (nav.wheel != 0.0f)
        m_scroll -= static_cast<int>(nav.wheel);
    if (lb.contains(nav.mouse) && nav.mouseMoved)
        m_hover = std::clamp(m_scroll + static_cast<int>((nav.mouse.y - lb.y - 2) / 18.0f), 0, n - 1);
    if (m_hover < m_scroll)
        m_scroll = m_hover;
    if (m_hover >= m_scroll + rows)
        m_scroll = m_hover - rows + 1;
    m_scroll = std::clamp(m_scroll, 0, std::max(0, n - rows));
    if (nav.accept || (nav.mousePressed && lb.contains(nav.mouse))) {
        set(m_hover);
        m_open = false;
    } else if (nav.back || (nav.mousePressed && !lb.contains(nav.mouse))) {
        m_open = false;
    }
}

void ValueBox::drawPopup(UiFrame& f) {
    if (!m_open)
        return;
    const auto opts = options();
    const Box lb = listBox(opts);
    const int rows = std::min<int>(static_cast<int>(opts.size()), 10);
    f.overlay.rect(lb.x - 1, lb.y - 1, lb.w + 2, lb.h + 2, rgba(110, 110, 240));
    f.overlay.rect(lb.x, lb.y, lb.w, lb.h, rgba(8, 2, 46, 245));
    const FontSpec font = style::valueFont();
    for (int i = 0; i < rows; ++i) {
        const int idx = m_scroll + i;
        if (idx >= static_cast<int>(opts.size()))
            break;
        const float y = lb.y + 2 + static_cast<float>(i) * 18.0f;
        if (idx == m_hover)
            f.overlay.rect(lb.x + 1, y, lb.w - 2, 18, rgba(60, 60, 200));
        f.text.draw(f.overlay, font, opts[static_cast<std::size_t>(idx)], lb.x + 8, y + 1,
                    idx == get() ? style::kValueTextFocus : style::kValueText);
    }
}

// --- TextBox -------------------------------------------------------------------------------

TextBox::TextBox(Box b, std::function<std::string()> t, Align a) : text(std::move(t)), align(a) { box = b; }

void TextBox::draw(UiFrame& f, bool) {
    const std::string s = text ? text() : std::string();
    const float lh = f.text.lineHeight(f.overlay, font);
    const float x = align == Align::Left ? box.x + 8 : (align == Align::Center ? box.x + box.w * 0.5f : box.x + box.w - 8);
    f.text.draw(f.overlay, font, s, x, box.y + (box.h - lh) * 0.5f, color, align);
}

// --- Slider ---------------------------------------------------------------------------------

Slider::Slider(Box row, std::function<float()> g, std::function<void(float)> s, float st)
    : get(std::move(g)), set(std::move(s)), step(st) {
    box = row;
}

namespace {
// Geometry inside a grid row: 25 px arrow buttons at both ends and the value
// bar between them (inferred from the sprite sizes and the 139 px grid boxes).
constexpr float kArrowW = 25.0f, kArrowH = 29.0f, kBarH = 19.0f;
} // namespace

void Slider::draw(UiFrame& f, bool focused) {
    const float y = box.y + (box.h - kArrowH) * 0.5f;
    const float barX = box.x + kArrowW, barW = box.w - 2 * kArrowW;
    const float v = std::clamp(get(), 0.0f, 1.0f);
    const char* left = balance ? "texture/slider_lbal.tga" : "texture/slider_larr.tga";
    const char* right = balance ? "texture/slider_rbal.tga" : "texture/slider_rarr.tga";
    const bool pressed = f.time < m_arrowPressedUntil;
    const int base = !enabled ? 4 : (focused ? 1 : 0);
    drawSpriteFrame(f, {left, 5}, pressed && m_arrowPressed < 0 ? 2 : base, box.x, y);
    drawSpriteFrame(f, {right, 5}, pressed && m_arrowPressed > 0 ? 2 : base, box.x + box.w - kArrowW, y);
    const UiTexture& bar = f.textures.get(focused ? "texture/slider_actl.tga" : "texture/slider_inactl.tga");
    if (bar) {
        // Crop the 500 px bar from its left end to the value.
        const float w = barW * v;
        const float u1 = w / static_cast<float>(bar.width);
        f.overlay.image(bar.handle, barX, box.y + (box.h - kBarH) * 0.5f, w, kBarH, {0, 1}, {u1, 0});
    }
}

bool Slider::adjust(UiFrame& f, int dir) {
    if (!enabled)
        return false;
    set(std::clamp(get() + step * static_cast<float>(dir), 0.0f, 1.0f));
    m_arrowPressed = dir;
    m_arrowPressedUntil = f.time + 0.1;
    return true;
}

void Slider::mouse(UiFrame& f, bool hovered) {
    if (!enabled)
        return;
    const float barX = box.x + kArrowW, barW = box.w - 2 * kArrowW;
    if (hovered && f.nav.mousePressed) {
        if (f.nav.mouse.x < barX)
            adjust(f, -1);
        else if (f.nav.mouse.x >= barX + barW)
            adjust(f, 1);
        else
            m_dragging = true;
    }
    if (!f.nav.mouseDown)
        m_dragging = false;
    if (m_dragging)
        set(std::clamp((f.nav.mouse.x - barX) / barW, 0.0f, 1.0f));
}

// --- ListBox --------------------------------------------------------------------------------

ListBox::ListBox(Box b, std::function<std::vector<std::string>()> it, std::function<int()> g, std::function<void(int)> s)
    : items(std::move(it)), get(std::move(g)), set(std::move(s)) {
    box = b;
}

bool ListBox::moveSelection(int dir) {
    const int n = static_cast<int>(items().size());
    if (n == 0)
        return false;
    const int cur = get();
    const int next = std::clamp(cur + dir, 0, n - 1);
    if (next == cur)
        return false;
    set(next);
    return true;
}

void ListBox::draw(UiFrame& f, bool focused) {
    const auto list = items();
    const int rows = std::max(1, static_cast<int>((box.h - 4) / rowHeight));
    const int sel = get();
    if (sel >= 0) {
        if (sel < m_scroll)
            m_scroll = sel;
        if (sel >= m_scroll + rows)
            m_scroll = sel - rows + 1;
    }
    m_scroll = std::clamp(m_scroll, 0, std::max(0, static_cast<int>(list.size()) - rows));
    const FontSpec font = style::valueFont();
    const Vec4 clip{box.x, box.y, box.w, box.h};
    for (int i = 0; i < rows; ++i) {
        const int idx = m_scroll + i;
        if (idx >= static_cast<int>(list.size()))
            break;
        const float y = box.y + 2 + static_cast<float>(i) * rowHeight;
        if (idx == sel)
            f.overlay.rect(box.x + 2, y, box.w - 4, rowHeight, focused ? rgba(70, 70, 230, 220) : rgba(50, 50, 150, 200));
        f.overlay.setClip(&clip);
        f.text.draw(f.overlay, font, list[static_cast<std::size_t>(idx)], box.x + 8, y + 1,
                    idx == sel && focused ? style::kValueTextFocus : style::kValueText);
        f.overlay.setClip(nullptr);
    }
    if (static_cast<int>(list.size()) > rows) {
        drawSpriteFrame(f, {"texture/scroll_uarr.tga", 4}, m_scroll > 0 ? 0 : 3, box.x + box.w - 21, box.y + 1);
        drawSpriteFrame(f, {"texture/scroll_darr.tga", 4},
                        m_scroll + rows < static_cast<int>(list.size()) ? 0 : 3, box.x + box.w - 21,
                        box.y + box.h - 22);
    }
}

void ListBox::mouse(UiFrame& f, bool hovered) {
    if (!hovered)
        return;
    const auto list = items();
    if (f.nav.wheel != 0.0f)
        m_scroll -= static_cast<int>(f.nav.wheel);
    if (!f.nav.mousePressed)
        return;
    const int rows = std::max(1, static_cast<int>((box.h - 4) / rowHeight));
    if (static_cast<int>(list.size()) > rows && f.nav.mouse.x >= box.x + box.w - 22) {
        m_scroll += f.nav.mouse.y < box.y + box.h * 0.5f ? -1 : 1;
        return;
    }
    const int row = m_scroll + static_cast<int>((f.nav.mouse.y - box.y - 2) / rowHeight);
    if (row >= 0 && row < static_cast<int>(list.size())) {
        const bool dbl = row == m_lastClickRow && f.time - m_lastClick < 0.4;
        set(row);
        m_lastClick = f.time;
        m_lastClickRow = row;
        if (dbl && onDoubleClick)
            onDoubleClick();
    }
}

// --- TextEntry ------------------------------------------------------------------------------

TextEntry::TextEntry(Box b, std::string* v, std::size_t maxLen) : value(v), maxLength(maxLen) { box = b; }

void TextEntry::draw(UiFrame& f, bool focused) {
    const FontSpec font = style::valueFont();
    const float lh = f.text.lineHeight(f.overlay, font);
    const float y = box.y + (box.h - lh) * 0.5f;
    const float w = f.text.draw(f.overlay, font, *value, box.x + 8, y,
                                m_editing || focused ? style::kValueTextFocus : style::kValueText);
    if (m_editing && std::fmod(f.time, 1.0) < 0.6)
        f.overlay.rect(box.x + 9 + w, y + 1, 2, lh - 2, style::kValueTextFocus);
}

bool TextEntry::activate(UiFrame&) {
    m_editing = !m_editing;
    if (!m_editing && onCommit)
        onCommit();
    return true;
}

void TextEntry::modalInput(UiFrame& f) {
    for (char c : f.nav.text) {
        // Printable ASCII and UTF-8 continuation bytes; the fonts cover Latin-1.
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && value->size() < maxLength)
            value->push_back(c);
    }
    if (f.nav.backspace && !value->empty()) {
        // Remove one UTF-8 code point.
        std::size_t n = value->size() - 1;
        while (n > 0 && (static_cast<unsigned char>((*value)[n]) & 0xC0) == 0x80)
            --n;
        value->resize(n);
    }
    // Enter commits; Escape also ends editing (keeping the text).
    if (f.nav.enter || f.nav.back || (f.nav.mousePressed && !box.contains(f.nav.mouse))) {
        m_editing = false;
        if (onCommit)
            onCommit();
    }
}

// --- Picture -----------------------------------------------------------------------------------

Picture::Picture(Box b, std::function<std::string()> p) : path(std::move(p)) { box = b; }

void Picture::draw(UiFrame& f, bool) {
    const std::string p = path ? path() : std::string();
    if (p.empty())
        return;
    drawImage(f.overlay, f.textures.get(p), box.x, box.y, box.w, box.h);
}

// --- Menu ----------------------------------------------------------------------------------------

Widget* Menu::focused() const {
    return m_focus >= 0 && m_focus < static_cast<int>(m_widgets.size()) ? m_widgets[static_cast<std::size_t>(m_focus)].get()
                                                                          : nullptr;
}

void Menu::focus(const Widget* w) {
    for (std::size_t i = 0; i < m_widgets.size(); ++i)
        if (m_widgets[i].get() == w)
            m_focus = static_cast<int>(i);
}

bool Menu::modalActive() const {
    const Widget* w = focused();
    return w && w->modal();
}

void Menu::moveFocus(Vec2 dir) {
    const Widget* cur = focused();
    if (!cur) {
        // Initial focus: the top-left content widget (below the navigation
        // strip at the top of the screen).
        int best = -1;
        float bestKey = 1e30f;
        for (std::size_t i = 0; i < m_widgets.size(); ++i) {
            const Widget& w = *m_widgets[i];
            if (!w.focusable())
                continue;
            const float key = (w.box.y < 45.0f ? 1e6f : 0.0f) + w.box.y * 4.0f + w.box.x;
            if (key < bestKey) {
                bestKey = key;
                best = static_cast<int>(i);
            }
        }
        m_focus = best;
        return;
    }
    const Vec2 c = cur->box.center();
    int best = -1;
    float bestScore = 1e30f;
    for (std::size_t i = 0; i < m_widgets.size(); ++i) {
        const Widget& w = *m_widgets[i];
        if (&w == cur || !w.focusable())
            continue;
        const Vec2 d = w.box.center() - c;
        const float along = d.x * dir.x + d.y * dir.y;
        if (along <= 1.0f)
            continue;
        const float across = std::abs(d.x * dir.y - d.y * dir.x);
        const float score = along + across * 2.5f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    if (best >= 0)
        m_focus = best;
}

void Menu::update(UiFrame& f) {
    const NavInput& nav = f.nav;
    if (m_focus < 0 || !focused() || !focused()->focusable())
        moveFocus({0, 1});
    Widget* cur = focused();
    if (cur && cur->modal()) {
        cur->modalInput(f);
        return;
    }

    // Mouse: hover focuses, click activates.
    Widget* hovered = nullptr;
    for (auto& w : m_widgets)
        if (w->focusable() && w->box.contains(nav.mouse))
            hovered = w.get();
    if (hovered && (nav.mouseMoved || nav.mousePressed)) {
        focus(hovered);
        cur = hovered;
    }
    if (cur)
        cur->mouse(f, cur == hovered);
    if (cur && hovered == cur && nav.mousePressed && !dynamic_cast<ListBox*>(cur) && !dynamic_cast<Slider*>(cur)) {
        cur->activate(f);
        return;
    }

    if (nav.back) {
        if (onBack)
            onBack();
        return;
    }
    if (!cur)
        return;
    auto* list = dynamic_cast<ListBox*>(cur);
    if (nav.up && !(list && list->moveSelection(-1)))
        moveFocus({0, -1});
    if (nav.down && !(list && list->moveSelection(1)))
        moveFocus({0, 1});
    if (nav.left && !cur->adjust(f, -1))
        moveFocus({-1, 0});
    if (nav.right && !cur->adjust(f, 1))
        moveFocus({1, 0});
    if (nav.tabNext || nav.tabPrev) {
        const int n = static_cast<int>(m_widgets.size());
        for (int step = 1; step <= n; ++step) {
            const int i = ((m_focus + (nav.tabNext ? step : -step)) % n + n) % n;
            if (m_widgets[static_cast<std::size_t>(i)]->focusable()) {
                m_focus = i;
                break;
            }
        }
    }
    if (nav.accept && focused())
        focused()->activate(f);
}

void Menu::draw(UiFrame& f) {
    if (!background.empty())
        drawImage(f.overlay, f.textures.get(background), 0, 0, 640, 480);
    drawContent(f);
}

void Menu::drawContent(UiFrame& f) {
    const Widget* cur = focused();
    for (auto& w : m_widgets)
        if (w->visible)
            w->draw(f, w.get() == cur);
    const std::string& helpPic = cur && !cur->help.empty() ? cur->help : defaultHelp;
    if (!helpPic.empty())
        drawImage(f.overlay, f.textures.get(helpPic), helpBox.x, helpBox.y, helpBox.w, helpBox.h);
    for (auto& w : m_widgets)
        if (w->visible)
            w->drawPopup(f);
}

} // namespace mm2::ui
