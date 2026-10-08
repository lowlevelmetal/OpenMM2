// The in-race popup's OPTIONS pages (MM2 PUOptions, PUAudioOptions, PUControl,
// PUGraphics) and the PUMenuBase layout of the popup menus. See
// PopupOptions.h.
#include "app/frontend/PopupOptions.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace mm2::app::frontend {
namespace popup {

std::uint32_t cardColor() { return render::packColor(0x10, 0x1F, 0x5D, 0x80); }

ui::Box cardFor(PopupPage page) {
    if (page == PopupPage::KeyMap)
        return kKeyCard;
    return page == PopupPage::Control || page == PopupPage::Graphics ? kWideCard : kCard;
}

ui::Box at(const ui::Box& card, float x, float y, float w, float h) {
    return {card.x + x * card.w, card.y + y * card.h, w * card.w, h * card.h};
}

ui::TextButton& addButton(ui::Menu& menu, const ui::Box& card, float x, float y, float w, float h,
                          std::string label, int type, std::function<void()> onClick) {
    auto& b = menu.add<ui::TextButton>(at(card, x, y, w, h), std::move(label), std::move(onClick));
    b.type = type;
    b.font = ui::style::popupButtonFont(); // PUMenuBase +0xa0: GetFont 24
    return b;
}

void drawCard(render::Overlay2D& overlay, const ui::Box& card) {
    overlay.rect(card.x, card.y, card.w, card.h, cardColor());
}

void drawTitle(ui::UiFrame& f, const ui::Box& card, std::string_view title) {
    // UIMenu::AddLabel(0x68, name, 0, 0, 1, 0.1, 0, 32): label flags 0 give
    // the text effects DT_VCENTER | DT_SINGLELINE, left-aligned.
    const ui::Box line = at(card, 0.0f, 0.0f, 1.0f, kButtonHeight);
    const auto font = ui::style::popupTitleFont();
    const float lh = f.text.lineHeight(f.overlay, font);
    f.text.draw(f.overlay, font, title, line.x, line.y + (line.h - lh) * 0.5f, ui::style::kPopupText);
}

std::optional<int> lightQualityFromSlider(float slider, int current) {
    const float cur = static_cast<float>(current);
    if (slider == cur)
        return std::nullopt;
    // Read from the asm (the decompile loses the x87 values): raised, the
    // whole part of slider + 1; lowered, the whole part of the slider; then
    // kept within the slider's 1..3.
    const int q = static_cast<int>(slider > cur ? slider + 1.0f : slider);
    return std::clamp(q, 1, 3);
}

void setRWStates(ControlStates& s, controls::Controller c, bool doingFF) {
    using controls::Controller;
    s = {};
    if (c == Controller::Wheel || c == Controller::Joystick) {
        if (doingFF)
            s.collision = s.roadForce = true;
        s.sensitivity = s.deadZone = true;
    } else if (c == Controller::Mouse) {
        s.sensitivity = true;
    }
}

void initSensitivity(ControlStates& s, controls::Controller c) {
    using controls::Controller;
    s.sensitivity = s.deadZone = true;
    if (c == Controller::Keyboard || c == Controller::GamePad)
        s.sensitivity = s.deadZone = false;
}

std::vector<int> keyMapSlots(controls::Controller c) {
    using controls::Controller;
    constexpr int kSlots = static_cast<int>(controls::Action::Count); // 34
    std::array<bool, kSlots> on{};
    on.fill(true);
    if (c == Controller::Keyboard) {
        on[5] = false;  // Steering
        on[31] = false; // Camera Pan
    } else {
        on[6] = false;  // Steer Left
        on[7] = false;  // Steer Right
        on[31] = false; // Camera Pan
    }
    const int count = static_cast<int>(std::count(on.begin(), on.end(), true));
    const int walked = std::min(kSlots, (count < 32 ? 1 : 0) + 33);
    std::vector<int> out;
    for (int i = 0; i < walked; ++i)
        if (on[static_cast<std::size_t>(i)])
            out.push_back(i);
    return out;
}

} // namespace popup

namespace {

using popup::addButton;
using popup::at;

// The [Graphics] defaults (the options page's, mmGfxCFG::AutoDetect's top
// tier) and the [Controls] defaults (mmPlayerConfig::DefaultControls).
constexpr int kDefaultObjectDetail = 3;
constexpr double kDefaultFarClip = 1000.0;
constexpr int kDefaultLighting = 3;
constexpr int kDefaultCloudShadows = 2;

float iniFloat(const IniFile& ini, const char* section, const char* key, double def, float lo, float hi) {
    return std::clamp(static_cast<float>(ini.getDouble(section, key, def)), lo, hi);
}
int iniInt(const IniFile& ini, const char* section, const char* key, int def, int lo, int hi) {
    return static_cast<int>(std::clamp<long long>(ini.getInt(section, key, def), lo, hi));
}

// A UISlider with its label above (AddSlider with label mode -1): the slider
// row starts the popup line height below the label; the row is the
// mmSlider's arrows' 29 px.
ui::Slider& addSlider(ui::Menu& menu, const ui::Box& card, float x, float y, float w, std::string label,
                      std::function<float()> get, std::function<void(float)> set, float lo, float hi) {
    const ui::Box b = at(card, x, y, w, 0.0f);
    auto& s = menu.add<ui::Slider>(ui::Box{b.x, b.y, b.w, ui::style::kPopupLineHeight + 29.0f}, std::move(get),
                                   std::move(set), lo, hi);
    s.label = std::move(label);
    s.labelHeight = ui::style::kPopupLineHeight;
    return s;
}

// A UITextDropdown with its label above (AddTextDropdown with label mode 1):
// the box starts the popup line height below the label.
ui::ValueBox& addDropdown(ui::Menu& menu, const ui::Box& card, float x, float y, float w, std::string label,
                          std::vector<std::string> options, std::function<int()> get, std::function<void(int)> set) {
    const ui::Box b = at(card, x, y, w, 0.0f);
    auto& v = menu.add<ui::ValueBox>(ui::Box{b.x, b.y + ui::style::kPopupLineHeight, b.w, 0.0f},
                                     [o = std::move(options)] { return o; }, std::move(get), std::move(set));
    v.popup = true;
    v.label = std::move(label);
    v.labelHeight = ui::style::kPopupLineHeight;
    return v;
}

} // namespace

std::unique_ptr<ui::Menu> PopupOptions::build(PopupPage page, const PopupOptionsHost& host) {
    auto menu = std::make_unique<ui::Menu>();
    menu->popupSounds = true;
    switch (page) {
    case PopupPage::Options: buildOptions(*menu, host); break;
    case PopupPage::Audio: buildAudio(*menu, host); break;
    case PopupPage::Control: buildControl(*menu, host); break;
    case PopupPage::Graphics: buildGraphics(*menu, host); break;
    case PopupPage::KeyMap: buildKeyMap(*menu, host); break;
    }
    return menu;
}

void PopupOptions::draw(PopupPage page, ui::UiFrame& f) const {
    const ui::Box card = popup::cardFor(page);
    popup::drawCard(f.overlay, card);
    // PUOptions calls CreateTitle(0): no title. The others name themselves
    // (strings 442, 448, 460) and call CreateTitle(1).
    const auto& s = m_ctx.game->strings;
    switch (page) {
    case PopupPage::Options: break;
    case PopupPage::Audio: popup::drawTitle(f, card, s.get(442, "Audio Options")); break;
    case PopupPage::Control: popup::drawTitle(f, card, s.get(448, "Control Options")); break;
    case PopupPage::Graphics: popup::drawTitle(f, card, s.get(460, "Graphics Options")); break;
    case PopupPage::KeyMap: drawKeyMap(f); break;
    }
}

// PUOptions (menu 5): Previous Menu (AddPrevious: id 100 at the exit's place,
// 0.5, 0.9, 0.5 x 0.1, type 1), then Audio, Control and Graphics Options
// (strings 475-477; ids 0x65-0x67) across the card at 0.2, 0.4 and 0.6, type
// 2. mmPopup::Update: 0x65-0x67 switch to menus 6-8, Previous Menu and
// Escape to PUMain (menu 1).
void PopupOptions::buildOptions(ui::Menu& menu, const PopupOptionsHost& host) {
    const auto& s = m_ctx.game->strings;
    const ui::Box card = popup::kCard;
    auto back = [show = host.show] { show(std::nullopt); };
    auto& previous = addButton(menu, card, 0.5f, 0.9f, 0.5f, popup::kButtonHeight, s.get(470, "Previous Menu"), 1, back);
    auto open = [show = host.show](PopupPage p) { return [show, p] { show(p); }; };
    addButton(menu, card, 0.0f, 0.2f, 1.0f, popup::kButtonHeight, s.get(475, "Audio Options"), 2,
              open(PopupPage::Audio));
    addButton(menu, card, 0.0f, 0.4f, 1.0f, popup::kButtonHeight, s.get(476, "Control Options"), 2,
              open(PopupPage::Control));
    addButton(menu, card, 0.0f, 0.6f, 1.0f, popup::kButtonHeight, s.get(477, "Graphics Options"), 2,
              open(PopupPage::Graphics));
    menu.setInitialFocus(&previous); // SetBstate(0): the first widget
    menu.onBack = back;
}

// PUMenuBase::AddOKCancel: Cancel (string 471, id 0x66) at (0, 0.9) and OK
// (472, id 0x67) at (0.6, 0.9), 0.4 x 0.1, type 1; both return to PUOptions
// (mmPopup::Update, menus 6-8), Cancel after the page's CancelAction. Escape
// does nothing on these pages: UIMenu::BackUp sets the menu state mmPopup
// leaves unhandled for menus 6-8.
void PopupOptions::addOkCancel(ui::Menu& menu, const ui::Box& card, const PopupOptionsHost& host,
                               std::function<void()> cancel) {
    const auto& s = m_ctx.game->strings;
    auto& c = addButton(menu, card, 0.0f, 0.9f, 0.4f, popup::kButtonHeight, s.get(471, "Cancel"), 1,
                        [this, show = host.show, cancel = std::move(cancel)] {
                            if (cancel)
                                cancel();
                            // OpenMM2 keeps the options in openmm2.ini and
                            // writes them as the page closes; MM2 writes the
                            // driver's configuration when the game ends
                            // (mmGame::BeDone).
                            m_ctx.saveSettings();
                            show(PopupPage::Options);
                        });
    addButton(menu, card, 0.6f, 0.9f, 0.4f, popup::kButtonHeight, s.get(472, "OK"), 1, [this, show = host.show] {
        m_ctx.saveSettings();
        show(PopupPage::Options);
    });
    menu.setInitialFocus(&c); // SetBstate(0) is the title label, which cannot take the focus
    menu.onBack = [] {};
}

// PUAudioOptions (menu 6, title 442): SOUND FX VOLUME (443), MUSIC/CITY VOLUME
// (444) and BALANCE (445, -1..1 with the balance arrows) as labelled sliders
// at x 0.05, 0.6 wide, from y 0.11 every 2 x WIDGET_HEIGHT (1/15) + 0.11.
// Each change goes to the audio manager at once (SetWaveVolume,
// SetCDVolume, SetBalance); CANCEL puts back the settings the page opened
// with (PreSetup's mmPlayerConfig::GetAudio, CancelAction's SetAudio).
void PopupOptions::buildAudio(ui::Menu& menu, const PopupOptionsHost& host) {
    Context& ctx = m_ctx;
    const auto& s = ctx.game->strings;
    const ui::Box card = popup::kCard;
    m_saved = ctx.settings;
    addOkCancel(menu, card, host, [this, &ctx] {
        auto& st = ctx.settings;
        st.effectsVolume = m_saved.effectsVolume;
        st.engineVolume = m_saved.engineVolume;
        st.voiceVolume = m_saved.voiceVolume;
        st.musicVolume = m_saved.musicVolume;
        st.balance = m_saved.balance;
        ctx.applyAudioSettings();
    });
    constexpr float kStep = 2.0f / 15.0f + 0.11f; // UIMenu::WIDGET_HEIGHT * 2 + 0.11
    float y = popup::kTitleBottom;
    auto& st = ctx.settings;
    addSlider(menu, card, 0.05f, y, 0.6f, s.get(443, "Sound FX Volume"), [&st] { return st.effectsVolume; },
              [&ctx](float v) {
                  ctx.settings.effectsVolume = ctx.settings.engineVolume = ctx.settings.voiceVolume = v;
                  ctx.applyAudioSettings();
              },
              0.0f, 1.0f);
    y += kStep;
    addSlider(menu, card, 0.05f, y, 0.6f, s.get(444, "Music/City Volume"), [&st] { return st.musicVolume; },
              [&ctx](float v) {
                  ctx.settings.musicVolume = v;
                  ctx.applyAudioSettings();
              },
              0.0f, 1.0f);
    y += kStep;
    addSlider(menu, card, 0.05f, y, 0.6f, s.get(445, "Balance"), [&st] { return st.balance; },
              [&ctx](float v) {
                  ctx.settings.balance = v;
                  ctx.applyAudioSettings();
              },
              -1.0f, 1.0f)
        .balance = true;
}

controls::Controller PopupOptions::controller() const {
    return static_cast<controls::Controller>(
        iniInt(m_ctx.settings.ini, "Controls", "Controller", static_cast<int>(controls::Controller::Keyboard), 0, 4));
}

bool PopupOptions::doingForceFeedback() const {
    // mmInput::DoingFF: a force-feedback device, the FORCE FEEDBACK option
    // and a joystick or wheel. Inferred: any connected joystick or game pad
    // stands for MM2's force-feedback device (OpenMM2 does not query rumble
    // support), as on the Control options page.
    const bool device = !m_ctx.input.gamepads().empty() || !m_ctx.input.joysticks().empty();
    const auto c = controller();
    return device && m_ctx.settings.ini.getBool("Controls", "ForceFeedback", false) &&
           (c == controls::Controller::Joystick || c == controls::Controller::Wheel);
}

void PopupOptions::applyControlStates() {
    // vtable Disable / Enable on the sliders: greyed and skipped by the focus.
    m_sensitivity->enabled = m_controlStates.sensitivity;
    m_deadZone->enabled = m_controlStates.deadZone;
    m_collision->enabled = m_controlStates.collision;
    m_roadForce->enabled = m_controlStates.roadForce;
}

// PUControl (menu 7, title 448, card 0.9 x 0.8): STEERING SENSITIVITY
// (449, 0.5..2) and COLLISION INTENSITY (451, 0..2) at y 0.36, CONTROLLER
// DEAD ZONE (450, 0..0.33) and ROAD FORCE INTENSITY (452, 0..2) at 0.61, in
// columns at x 0.05 and 0.55, 0.4 wide; then the CONTROL drop-down (453, the
// five controller types 580-584) at 0.05, 0.11. The values are mmInput's
// (+0x1C4 is the inverse of the sensitivity shown, +0x1AC, +0x1B4, +0x1B0),
// OpenMM2's [Controls] keys. CANCEL puts back the settings the page opened
// with (mmPlayerConfig::GetControls / SetControls).
void PopupOptions::buildControl(ui::Menu& menu, const PopupOptionsHost& host) {
    Context& ctx = m_ctx;
    const auto& s = ctx.game->strings;
    const ui::Box card = popup::kWideCard;
    m_saved = ctx.settings;
    addOkCancel(menu, card, host, [this, &ctx, changed = host.controlsChanged] {
        ctx.settings.ini = m_saved.ini;
        popup::initSensitivity(m_controlStates, controller());
        if (changed)
            changed();
    });
    auto& ini = ctx.settings.ini;
    auto slider = [&](float x, float y, std::uint32_t id, const char* fallback, const char* key, double def, float lo,
                      float hi) -> ui::Slider& {
        return addSlider(
            menu, card, x, y, 0.4f, s.get(id, fallback), [&ini, key, def, lo, hi] { return iniFloat(ini, "Controls", key, def, lo, hi); },
            [&ini, key, changed = host.controlsChanged](float v) {
                ini.setDouble("Controls", key, v);
                if (changed)
                    changed();
            },
            lo, hi);
    };
    const float row1 = popup::kTitleBottom + 0.25f, row2 = row1 + 0.25f;
    m_sensitivity = &slider(0.05f, row1, 449, "Steering Sensitivity", "Sensitivity", 1.0, 0.5f, 2.0f);
    m_collision = &slider(0.55f, row1, 451, "Collision Intensity", "FFCollision", 1.0, 0.0f, 2.0f);
    m_deadZone = &slider(0.05f, row2, 450, "Controller Dead Zone", "DeadZone", 0.1, 0.0f, 0.33f);
    m_roadForce = &slider(0.55f, row2, 452, "Road Force Intensity", "FFRoadForce", 1.0, 0.0f, 2.0f);
    addDropdown(menu, card, 0.05f, popup::kTitleBottom, 0.4f, s.get(453, "CONTROL"),
                {s.get(580, "Mouse"), s.get(581, "Keyboard"), s.get(582, "Joystick"), s.get(583, "Game Pad"),
                 s.get(584, "Steering Wheel")},
                [this] { return static_cast<int>(controller()); },
                [this, changed = host.controlsChanged](int i) {
                    // PUControl::ControlSelect: mmInput::Init with the new
                    // device, then InitSensitivity and SetRWStates.
                    m_ctx.settings.ini.setInt("Controls", "Controller", i);
                    if (changed)
                        changed();
                    popup::initSensitivity(m_controlStates, controller());
                    popup::setRWStates(m_controlStates, controller(), doingForceFeedback());
                    applyControlStates();
                });
    // PUControl::PreSetup: SetRWStates, then InitSensitivity.
    popup::setRWStates(m_controlStates, controller(), doingForceFeedback());
    popup::initSensitivity(m_controlStates, controller());
    applyControlStates();
}

// PUGraphics (menu 8, title 460, card 0.9 x 0.8). Left column at x 0.05, 0.4
// wide: OBJECT DETAIL (648, 574-577), VISIBILITY (461, the far clip
// 100..1000) and LIGHTING QUALITY (644, 1..3); right column at 0.55: CLOUD
// SHADOWS (659: 660, 661, 576), VEHICLE REFLECTIONS (647) and TEXTURED SKY
// (645) as text toggles. Rows at 0.11, 0.385 and 0.66 (each 0.075 + 0.2
// below the last). Every change applies at once (mmGame::FarClipCB,
// mmGame::SetLevelGraphics, lvlLevel::SetObjectDetail); CANCEL keeps it
// (PUGraphics::CancelAction is empty).
void PopupOptions::buildGraphics(ui::Menu& menu, const PopupOptionsHost& host) {
    Context& ctx = m_ctx;
    const auto& s = ctx.game->strings;
    const ui::Box card = popup::kWideCard;
    addOkCancel(menu, card, host, {});
    auto& ini = ctx.settings.ini;
    auto changed = host.graphicsChanged;
    auto apply = [changed] {
        if (changed)
            changed();
    };
    const float row0 = popup::kTitleBottom;
    const float row1 = row0 + popup::kWidgetHeight + 0.2f;
    const float row2 = row1 + popup::kWidgetHeight + 0.2f;
    addDropdown(menu, card, 0.05f, row0, 0.4f, s.get(648, "Object Detail"),
                {s.get(574, "Low"), s.get(575, "Medium"), s.get(576, "High"), s.get(577, "Very High")},
                [&ini] { return iniInt(ini, "Graphics", "ObjectDetail", kDefaultObjectDetail, 0, 3); },
                [&ini, apply](int i) {
                    ini.setInt("Graphics", "ObjectDetail", i);
                    apply();
                });
    addSlider(menu, card, 0.05f, row1, 0.4f, s.get(461, "Visibility"),
              [&ini] { return iniFloat(ini, "Graphics", "FarClip", kDefaultFarClip, 100.0f, 1000.0f); },
              [&ini, apply](float v) {
                  ini.setDouble("Graphics", "FarClip", v); // PUGraphics::FixClip
                  apply();
              },
              100.0f, 1000.0f);
    addSlider(menu, card, 0.05f, row2, 0.4f, s.get(644, "Lighting Quality"),
              [&ini] { return static_cast<float>(iniInt(ini, "Graphics", "LightingQuality", kDefaultLighting, 0, 3)); },
              [&ini, apply](float v) {
                  const int cur = iniInt(ini, "Graphics", "LightingQuality", kDefaultLighting, 0, 3);
                  if (const auto q = popup::lightQualityFromSlider(v, cur)) {
                      ini.setInt("Graphics", "LightingQuality", *q);
                      apply();
                  }
              },
              1.0f, 3.0f);
    addDropdown(menu, card, 0.55f, row0, 0.4f, s.get(659, "Cloud Shadows"),
                {s.get(660, "None"), s.get(661, "Low"), s.get(576, "High")},
                [&ini] { return iniInt(ini, "Graphics", "CloudShadows", kDefaultCloudShadows, 0, 2); },
                [&ini, apply](int i) {
                    ini.setInt("Graphics", "CloudShadows", i);
                    apply();
                });
    auto toggle = [&](float y, std::uint32_t id, const char* fallback, const char* key) {
        auto& t = menu.add<ui::TextToggle>(
            at(card, 0.55f, y, 0.4f, popup::kWidgetHeight), s.get(id, fallback),
            [&ini, key] { return ini.getBool("Graphics", key, true); },
            [&ini, key, apply] {
                ini.setBool("Graphics", key, !ini.getBool("Graphics", key, true));
                apply();
            });
        t.stateWidth = popup::kWidgetHeight * card.w; // UIToggleButton2::Init: the last 0.075
        t.onText = s.get(607, "ON");
        t.offText = s.get(606, "OFF");
    };
    toggle(row1, 647, "Vehicle Reflections", "VehicleReflections");
    toggle(row2, 645, "Textured Sky", "TexturedSky");
}

// PUKey (menu 11, F1 in the race: mmGame::Update and UpdatePaused call
// mmPopup::ProcessKeymap): no title, Resume Driving at the exit's place,
// which closes the popup, as Escape does (mmPopup::Update, menu 11).
void PopupOptions::buildKeyMap(ui::Menu& menu, const PopupOptionsHost& host) {
    const auto& s = m_ctx.game->strings;
    auto close = host.close;
    auto& resume = addButton(menu, popup::kKeyCard, 0.5f, 0.9f, 0.5f, popup::kButtonHeight,
                             s.get(473, "Resume Driving"), 1, [close] {
                                 if (close)
                                     close();
                             });
    menu.setInitialFocus(&resume);
    menu.onBack = [close] {
        if (close)
            close();
    };
}

// PUKey::PreSetup: one text node at (0.05, 0.075) of the screen, 0.9 x 0.9;
// the actions the controller uses (popup::keyMapSlots) in two columns, the
// name ("%-23s") at x 0.05 and the binding ("%.23s", mmIO::GetDescription)
// at 0.25 for the left column, 0.5 and 0.7 for the right one, the rows
// from y 0.05 every 0.03 of the screen, in GetFont 16. OpenMM2 binds only
// keys: an action shows its key (or UNDEFINED, string 271), the steering
// axis of the analog devices the controller's name (inferred).
void PopupOptions::drawKeyMap(ui::UiFrame& f) const {
    const auto& s = m_ctx.game->strings;
    const auto c = controller();
    const auto font = ui::style::popupSmallFont();
    const float x0 = 0.05f * 640.0f, y0 = 0.075f * 480.0f;
    const auto& actions = controls::actions();
    const auto slots = popup::keyMapSlots(c);
    for (std::size_t k = 0; k < slots.size(); ++k) {
        const auto& a = actions[static_cast<std::size_t>(slots[k])];
        std::string key;
        if (!a.keyboard) {
            const std::uint32_t names[] = {580, 581, 582, 583, 584};
            key = s.get(names[static_cast<int>(c)]);
        } else {
            const platform::Key bound = controls::boundKey(m_ctx.settings.ini, a);
            key = bound == platform::Key::Unknown ? s.get(271, "UNDEFINED") : platform::keyName(bound);
        }
        if (key.size() > 23)
            key.resize(23);
        const bool right = k % 2 != 0;
        const float y = y0 + (0.05f + 0.03f * static_cast<float>(k / 2)) * 480.0f;
        f.text.draw(f.overlay, font, s.get(a.stringId), x0 + (right ? 0.5f : 0.05f) * 640.0f, y, ui::style::kPopupText);
        f.text.draw(f.overlay, font, key, x0 + (right ? 0.7f : 0.25f) * 640.0f, y, ui::style::kPopupText);
    }
}

// --- Sounds ------------------------------------------------------------------------------------

PopupSounds::PopupSounds(Context& ctx) : m_ctx(ctx) {
    m_fn = [this](std::string_view name, float volume) { play(name, volume); };
}

void PopupSounds::play(std::string_view name, float volume) {
    if (!m_ctx.mixer || !m_ctx.game || name.empty())
        return;
    if (!m_bank)
        m_bank = std::make_unique<audio::SoundBank>(m_ctx.game->vfs);
    auto it = m_slots.find(name);
    if (it == m_slots.end()) {
        audio::game::SoundSlot slot;
        slot.load(*m_ctx.mixer, *m_bank, name, audio::Bus::Effects);
        it = m_slots.emplace(std::string(name), std::move(slot)).first;
    }
    if (!it->second.valid())
        return;
    // MenuManager::PlaySound stops the sound and plays it again.
    it->second.stop();
    it->second.playOnce(volume);
}

// --- Automation ---------------------------------------------------------------------------------

std::optional<PopupScript> PopupScript::fromEnvironment() {
    const char* text = std::getenv("OPENMM2_POPUP_SCRIPT");
    if (!text || !*text)
        return std::nullopt;
    return PopupScript(text);
}

PopupScript::PopupScript(std::string_view text) {
    for (auto part : str::split(text, ';'))
        if (!str::trim(part).empty())
            m_commands.emplace_back(str::trim(part));
}

PopupScript::Step PopupScript::step() {
    Step out;
    if (m_wait > 0) {
        --m_wait;
        return out;
    }
    while (m_next < m_commands.size()) {
        const std::string& c = m_commands[m_next++];
        const auto colon = c.find(':');
        const std::string cmd = c.substr(0, colon);
        const std::string arg = colon == std::string::npos ? "" : c.substr(colon + 1);
        if (cmd == "wait") {
            m_wait = static_cast<int>(str::parseInt(arg).value_or(1));
            return out;
        }
        if (cmd == "open") {
            out.open = arg;
            return out;
        }
        if (cmd == "nav") {
            using platform::Key;
            out.key = arg == "up"       ? Key::Up
                      : arg == "down"   ? Key::Down
                      : arg == "left"   ? Key::Left
                      : arg == "right"  ? Key::Right
                      : arg == "accept" ? Key::Return
                      : arg == "back"   ? Key::Escape
                                        : Key::Unknown;
            return out;
        }
        log::warn("popup script: unknown command '{}'", cmd);
    }
    return out;
}

void injectKey(Context& ctx, platform::Key key) {
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = static_cast<SDL_Scancode>(key);
    down.key.down = true;
    ctx.input.handleEvent(down);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    // Released on the next frame so the press is seen once.
    SDL_PushEvent(&up);
}

} // namespace mm2::app::frontend
