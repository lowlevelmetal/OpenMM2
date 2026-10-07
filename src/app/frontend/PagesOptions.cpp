// Options: main options page (opt_bk), graphics (gfx_bk), audio (aud_bk),
// controls (ctrl_bk), customize controls (cuss_bk) and about (about_bk).
#include "app/frontend/Frontend.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Window.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::app::frontend {
namespace {

using ui::Box;
using ui::SpriteSheet;
using namespace layout;

// Options that exist in the original's menus but have no OpenMM2 subsystem
// yet are stored in the settings file so they survive until they do.
float iniFloat(Context& ctx, const char* section, const char* key, float def) {
    return std::clamp(static_cast<float>(ctx.settings.ini.getDouble(section, key, def)), 0.0f, 1.0f);
}
int iniInt(Context& ctx, const char* section, const char* key, int def) {
    return static_cast<int>(ctx.settings.ini.getInt(section, key, def));
}
bool iniBool(Context& ctx, const char* section, const char* key, bool def) {
    return ctx.settings.ini.getBool(section, key, def);
}

// Applies the audio toggles on top of Context::applyAudioSettings().
void applyAudio(Context& ctx) {
    ctx.applyAudioSettings();
    if (!ctx.mixer)
        return;
    if (!iniBool(ctx, "Audio", "SoundEffects", true)) {
        ctx.mixer->setBusVolume(audio::Bus::Effects, 0);
        ctx.mixer->setBusVolume(audio::Bus::Engine, 0);
    }
    if (!iniBool(ctx, "Audio", "Commentary", true))
        ctx.mixer->setBusVolume(audio::Bus::Voice, 0);
    if (!iniBool(ctx, "Audio", "Music", true))
        ctx.mixer->setBusVolume(audio::Bus::Music, 0);
    if (!iniBool(ctx, "Audio", "CitySounds", true))
        ctx.mixer->setBusVolume(audio::Bus::Ambient, 0);
}

// An options sub-page: CANCEL restores the settings captured on entry, DONE
// keeps and saves them.
class SettingsPage : public Page {
public:
    SettingsPage(Frontend& fe, const char* background) : m_savedSettings(fe.ctx.settings), m_savedDisplay(fe.ctx.display) {
        menu.background = background;
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_can.tga", 4}, kBack.x, kBack.y, [this, &fe] { cancel(fe); })
            .help = "jpg/opt_tbck.jpg";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_done.tga", 4}, kNext.x, kNext.y, [this, &fe] { done(fe); });
        menu.onBack = [this, &fe] { cancel(fe); };
        addNavStrip(fe, *this, false);
    }

protected:
    virtual void cancel(Frontend& fe) {
        fe.ctx.settings = m_savedSettings;
        applyAudio(fe.ctx);
        fe.pop();
    }
    virtual void done(Frontend& fe) {
        fe.ctx.saveSettings();
        fe.pop();
    }
    void addDefaults(Frontend& fe, float y, std::function<void()> reset) {
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_def.tga", 4}, 33, y, std::move(reset)).help = "jpg/opt_tdef.jpg";
        (void)fe;
    }

    Settings m_savedSettings;
    render::DisplaySettings m_savedDisplay;
};

// --- Options --------------------------------------------------------------------------

class OptionsPage final : public Page {
public:
    explicit OptionsPage(Frontend& fe) {
        menu.background = "jpg/opt_bk.jpg";
        menu.defaultHelp = "jpg/opt_tbck.jpg";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_aud.tga", 4}, kColumnX, kRow58,
                                   [&fe] { fe.push(makeAudioPage(fe)); })
            .help = "jpg/opt_taud.jpg";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_ctl.tga", 4}, kColumnX, kRow56,
                                   [&fe] { fe.push(makeControlPage(fe)); })
            .help = "jpg/opt_tctl.jpg";
        auto& gfx = menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_gfx.tga", 4}, kColumnX, kRow65,
                                               [&fe] { fe.push(makeGraphicsPage(fe)); });
        gfx.help = "jpg/opt_tgfx.jpg";
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_abt.tga", 4}, 25, 328, [&fe] { fe.push(makeAboutPage(fe)); })
            .help = "jpg/opt_tabt.jpg";
        addBack(fe, *this).help = "jpg/opt_tbck.jpg";
        addNavStrip(fe, *this, false);
        menu.focus(&gfx);
    }
};

// --- Graphics -------------------------------------------------------------------------

struct Resolution {
    int w, h;
    float hz;
};

class GraphicsPage final : public SettingsPage {
public:
    explicit GraphicsPage(Frontend& fe) : SettingsPage(fe, "jpg/gfx_bk.jpg"), m_pending(fe.ctx.display) {
        Context& ctx = fe.ctx;
        m_displays = platform::enumerateDisplays();
        menu.defaultHelp = "jpg/opt_tgfx.jpg";

        // DISPLAY: window mode.
        menu.add<ui::ValueBox>(
            Box{kBoxX, 62, kBoxWide, kBoxH},
            [] { return std::vector<std::string>{"Window", "Full Screen (Desktop)", "Full Screen (Exclusive)"}; },
            [this] { return static_cast<int>(m_pending.windowMode); },
            [this](int i) {
                m_pending.windowMode = static_cast<platform::WindowMode>(i);
                m_resolutions = resolutions();
            });
        // RENDERER
        menu.add<ui::ValueBox>(
            Box{kBoxX, 100, kBoxWide, kBoxH},
            [&ctx] {
                const std::string active = render::backendName(ctx.device().backend());
                return std::vector<std::string>{"Automatic (" + active + ")", "Vulkan", "OpenGL"};
            },
            [this] { return static_cast<int>(m_pending.backend); },
            [this](int i) { m_pending.backend = static_cast<render::Backend>(i); });
        // RESOLUTION
        m_resolutions = resolutions();
        menu.add<ui::ValueBox>(
            Box{kBoxX, 135, kBoxWide, kBoxH},
            [this] {
                std::vector<std::string> v;
                for (const auto& r : m_resolutions)
                    v.push_back(r.w == 0 ? std::string("Desktop")
                                         : (r.hz > 0 ? std::format("{} x {} ({:.0f} Hz)", r.w, r.h, r.hz)
                                                     : std::format("{} x {}", r.w, r.h)));
                return v;
            },
            [this] { return currentResolution(); }, [this](int i) { setResolution(i); });
        // Grid: VISIBILITY, LIGHTING QUALITY.
        menu.add<ui::Slider>(Box{471, 179, 139, 33}, [&ctx] { return iniFloat(ctx, "Graphics", "Visibility", 1.0f); },
                             [&ctx](float v) { ctx.settings.ini.setDouble("Graphics", "Visibility", v); });
        menu.add<ui::Slider>(Box{471, 212, 139, 33}, [&ctx] { return iniFloat(ctx, "Graphics", "Lighting", 1.0f); },
                             [&ctx](float v) { ctx.settings.ini.setDouble("Graphics", "Lighting", v); });
        const auto& s = ctx.game->strings;
        const std::vector<std::string> quality = {s.get(574, "Low"), s.get(575, "Medium"), s.get(576, "High"),
                                                  s.get(577, "Very High")};
        addChoice(ctx, Box{kBoxX, 261, kBoxWide, kBoxH}, "TextureQuality", quality, 3);
        addChoice(ctx, Box{kBoxX, 298, kBoxMid, 27}, "ObjectDetail", quality, 3);
        addChoice(ctx, Box{kBoxX, 333, kBoxMid, 27}, "CloudShadows",
                  {s.get(660, "None"), s.get(661, "Low"), s.get(662, "High")}, 2);

        addToggle(ctx, "texture/gfx_sky.tga", 64, "TexturedSky");
        addToggle(ctx, "texture/gfx_rflx.tga", 91, "VehicleReflections");
        addToggle(ctx, "texture/gfx_peds.tga", 118, "ShowPedestrians");
        addToggle(ctx, "texture/gfx_port.tga", 145, "SmartRendering");

        addAdvanced(fe);
        addDefaults(fe, 345, [this, &ctx] {
            m_pending = render::DisplaySettings{};
            m_pending.backend = ctx.display.backend;
            for (const char* k : {"Visibility", "Lighting", "TextureQuality", "ObjectDetail", "CloudShadows",
                                  "TexturedSky", "VehicleReflections", "ShowPedestrians", "SmartRendering"})
                ctx.settings.ini.remove("Graphics", k);
            m_resolutions = resolutions();
        });
    }

    void drawBelow(Frontend& fe, ui::UiFrame& f) override {
        // Panel for the options the original did not have (lower-left).
        f.overlay.rect(30, 192, 242, 188, render::packColor(8, 2, 46, 215));
        const auto font = ui::style::smallFont();
        const char* labels[] = {"VSYNC", "ANTI-ALIASING", "RENDER SCALE", "UI SCALE", "FIELD OF VIEW"};
        for (int i = 0; i < 5; ++i)
            f.text.draw(f.overlay, font, labels[i], 38, 200 + 28.0f * static_cast<float>(i) + 5, ui::style::kValueText);
        if (!m_notice.empty())
            f.text.drawWrapped(f.overlay, font, m_notice, 280, 380, 150, ui::style::kHelpText);
        (void)fe;
    }

protected:
    void cancel(Frontend& fe) override { SettingsPage::cancel(fe); }

    void done(Frontend& fe) override {
        Context& ctx = fe.ctx;
        m_pending.sanitize();
        const render::DisplaySettings old = ctx.display;
        // Backend, GPU and validation changes need a new renderer; everything
        // else applies live, between frames (outside any render pass).
        const bool restart = old.backend != m_pending.backend || old.gpu != m_pending.gpu ||
                             old.validation != m_pending.validation;
        ctx.afterFrame.push_back([&ctx, old, next = m_pending] { render::applyDisplaySettings(ctx.renderer, old, next); });
        ctx.display = m_pending;
        ctx.saveSettings();
        if (restart)
            fe.message("The new renderer takes effect after restarting OpenMM2.", [&fe] { fe.pop(); });
        else
            fe.pop();
    }

private:
    void addChoice(Context& ctx, Box b, const char* key, std::vector<std::string> options, int def) {
        menu.add<ui::ValueBox>(
            b, [options] { return options; },
            [&ctx, key, def, n = static_cast<int>(options.size())] {
                return std::clamp(iniInt(ctx, "Graphics", key, def), 0, n - 1);
            },
            [&ctx, key](int i) { ctx.settings.ini.setInt("Graphics", key, i); });
    }

    void addToggle(Context& ctx, const char* sprite, float y, const char* key) {
        menu.add<ui::LampItem>(
            SpriteSheet{sprite, 5}, kLampX, y, [&ctx, key] { return iniBool(ctx, "Graphics", key, true); },
            [&ctx, key] { ctx.settings.ini.setBool("Graphics", key, !iniBool(ctx, "Graphics", key, true)); });
    }

    void addAdvanced(Frontend& fe) {
        const float x = 140, w = 128, h = 24;
        auto row = [](int i) { return 200 + 28.0f * static_cast<float>(i); };
        menu.add<ui::ValueBox>(
            Box{x, row(0), w, h}, [] { return std::vector<std::string>{"Off", "On", "Adaptive", "Fast (Mailbox)"}; },
            [this] { return static_cast<int>(m_pending.vsync); },
            [this](int i) { m_pending.vsync = static_cast<render::VsyncMode>(i); });
        menu.add<ui::ValueBox>(
            Box{x, row(1), w, h},
            [&fe] {
                std::vector<std::string> v;
                for (auto s : fe.ctx.device().info().msaaSamples)
                    if (s <= 8)
                        v.push_back(s == 1 ? "Off" : std::format("{}x MSAA", s));
                return v;
            },
            [this, &fe] {
                const auto& s = fe.ctx.device().info().msaaSamples;
                for (std::size_t i = 0; i < s.size(); ++i)
                    if (s[i] == m_pending.msaa)
                        return static_cast<int>(i);
                return 0;
            },
            [this, &fe](int i) { m_pending.msaa = fe.ctx.device().info().msaaSamples[static_cast<std::size_t>(i)]; });
        static const float scales[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
        menu.add<ui::ValueBox>(
            Box{x, row(2), w, h},
            [] {
                std::vector<std::string> v;
                for (float s : scales)
                    v.push_back(std::format("{:.0f}%", s * 100));
                return v;
            },
            [this] {
                int best = 2;
                for (int i = 0; i < 6; ++i)
                    if (std::abs(scales[i] - m_pending.renderScale) < std::abs(scales[best] - m_pending.renderScale))
                        best = i;
                return best;
            },
            [this](int i) { m_pending.renderScale = scales[i]; });
        menu.add<ui::ValueBox>(
            Box{x, row(3), w, h}, [] { return std::vector<std::string>{"Fit (4:3)", "Stretch", "Pixel Exact"}; },
            [this] { return static_cast<int>(m_pending.uiScale); },
            [this](int i) { m_pending.uiScale = static_cast<render::UiScaleMode>(i); });
        menu.add<ui::ValueBox>(
            Box{x, row(4), w, h}, [] { return std::vector<std::string>{"Hor+ (wide)", "Vert- (4:3)", "Stretched"}; },
            [this] { return static_cast<int>(m_pending.fovMode); },
            [this](int i) { m_pending.fovMode = static_cast<render::FovMode>(i); });
    }

    std::vector<Resolution> resolutions() const {
        std::vector<Resolution> v;
        const int d = std::clamp(m_pending.display, 0, std::max(0, static_cast<int>(m_displays.size()) - 1));
        switch (m_pending.windowMode) {
        case platform::WindowMode::Borderless: v.push_back({0, 0, 0}); break;
        case platform::WindowMode::Fullscreen:
            v.push_back({0, 0, 0});
            if (!m_displays.empty())
                for (const auto& m : m_displays[static_cast<std::size_t>(d)].modes)
                    v.push_back({m.width, m.height, m.refreshRate});
            break;
        case platform::WindowMode::Windowed: {
            const int maxW = m_displays.empty() ? 7680 : m_displays[static_cast<std::size_t>(d)].width;
            const int maxH = m_displays.empty() ? 4320 : m_displays[static_cast<std::size_t>(d)].height;
            const int sizes[][2] = {{640, 480},   {800, 600},   {1024, 768},  {1280, 720},  {1280, 960},
                                    {1366, 768},  {1600, 900},  {1600, 1200}, {1920, 1080}, {2560, 1080},
                                    {2560, 1440}, {3440, 1440}, {3840, 2160}};
            for (const auto& s : sizes)
                if (s[0] <= maxW && s[1] <= maxH)
                    v.push_back({s[0], s[1], 0});
            if (std::ranges::none_of(v, [&](const Resolution& r) {
                    return r.w == m_pending.windowWidth && r.h == m_pending.windowHeight;
                }))
                v.push_back({m_pending.windowWidth, m_pending.windowHeight, 0});
            break;
        }
        }
        return v;
    }

    int currentResolution() const {
        for (std::size_t i = 0; i < m_resolutions.size(); ++i) {
            const auto& r = m_resolutions[i];
            if (m_pending.windowMode == platform::WindowMode::Windowed) {
                if (r.w == m_pending.windowWidth && r.h == m_pending.windowHeight)
                    return static_cast<int>(i);
            } else if (r.w == m_pending.fullscreenMode.width && r.h == m_pending.fullscreenMode.height &&
                       (r.hz == 0 || std::abs(r.hz - m_pending.fullscreenMode.refreshRate) < 0.5f)) {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    void setResolution(int i) {
        const auto& r = m_resolutions[static_cast<std::size_t>(i)];
        if (m_pending.windowMode == platform::WindowMode::Windowed) {
            m_pending.windowWidth = r.w;
            m_pending.windowHeight = r.h;
        } else if (r.w == 0) {
            m_pending.fullscreenMode = {};
        } else {
            for (const auto& d : m_displays)
                for (const auto& m : d.modes)
                    if (m.width == r.w && m.height == r.h && m.refreshRate == r.hz)
                        m_pending.fullscreenMode = m;
        }
    }

    render::DisplaySettings m_pending;
    std::vector<platform::DisplayInfo> m_displays;
    std::vector<Resolution> m_resolutions;
    std::string m_notice;
};

// --- Audio --------------------------------------------------------------------------------

class AudioPage final : public SettingsPage {
public:
    explicit AudioPage(Frontend& fe) : SettingsPage(fe, "jpg/aud_bk.jpg") {
        Context& ctx = fe.ctx;
        menu.defaultHelp = "jpg/opt_taud.jpg";
        auto& dev = menu.add<ui::ValueBox>(
            Box{kBoxX, 62, kBoxWide, kBoxH},
            [&ctx] {
                const std::string n = ctx.audioDevice.deviceName();
                return std::vector<std::string>{n.empty() ? std::string("No sound device") : n};
            },
            [] { return 0; }, [](int) {});
        dev.enabled = false;
        const auto& s = ctx.game->strings;
        menu.add<ui::ValueBox>(
            Box{kBoxX, 100, kBoxMid, kBoxH},
            [s1 = s.get(326, "Mono"), s2 = s.get(327, "Stereo")] { return std::vector<std::string>{s1, s2}; },
            [&ctx] { return iniBool(ctx, "Audio", "Stereo", true) ? 1 : 0; },
            [&ctx](int i) { ctx.settings.ini.setBool("Audio", "Stereo", i == 1); });
        menu.add<ui::ValueBox>(
            Box{kBoxX, 134, kBoxMid, kBoxH},
            [lo = s.get(390, "Low"), hi = s.get(392, "High")] { return std::vector<std::string>{lo, hi}; },
            [&ctx] { return ctx.settings.audioHighQuality ? 1 : 0; },
            [&ctx](int i) { ctx.settings.audioHighQuality = i == 1; });
        // SOUND FX VOLUME drives every effects bus; MUSIC/CITY drives music and
        // city ambience ("The original had one Music/City volume").
        menu.add<ui::Slider>(Box{471, 210, 139, 33}, [&ctx] { return ctx.settings.effectsVolume; },
                             [&ctx](float v) {
                                 ctx.settings.effectsVolume = ctx.settings.engineVolume = ctx.settings.voiceVolume = v;
                                 applyAudio(ctx);
                             });
        menu.add<ui::Slider>(Box{471, 243, 139, 33}, [&ctx] { return ctx.settings.musicVolume; },
                             [&ctx](float v) {
                                 ctx.settings.musicVolume = ctx.settings.ambientVolume = v;
                                 applyAudio(ctx);
                             });
        menu.add<ui::Slider>(Box{471, 276, 139, 33}, [&ctx] { return iniFloat(ctx, "Audio", "Balance", 0.5f); },
                             [&ctx](float v) { ctx.settings.ini.setDouble("Audio", "Balance", v); })
            .balance = true;
        addToggle(ctx, "texture/aud_fx.tga", 64, "SoundEffects");
        addToggle(ctx, "texture/aud_com.tga", 91, "Commentary");
        addToggle(ctx, "texture/aud_musc.tga", 125, "Music");
        addToggle(ctx, "texture/aud_amb.tga", 152, "CitySounds");
        addDefaults(fe, 345, [&ctx] {
            const Settings d;
            ctx.settings.effectsVolume = d.effectsVolume;
            ctx.settings.engineVolume = d.engineVolume;
            ctx.settings.voiceVolume = d.voiceVolume;
            ctx.settings.musicVolume = d.musicVolume;
            ctx.settings.ambientVolume = d.ambientVolume;
            ctx.settings.audioHighQuality = d.audioHighQuality;
            for (const char* k : {"Stereo", "Balance", "SoundEffects", "Commentary", "Music", "CitySounds"})
                ctx.settings.ini.remove("Audio", k);
            applyAudio(ctx);
        });
    }

private:
    void addToggle(Context& ctx, const char* sprite, float y, const char* key) {
        menu.add<ui::LampItem>(
            SpriteSheet{sprite, 5}, kLampX, y, [&ctx, key] { return iniBool(ctx, "Audio", key, true); },
            [&ctx, key] {
                ctx.settings.ini.setBool("Audio", key, !iniBool(ctx, "Audio", key, true));
                applyAudio(ctx);
            });
    }
};

// --- Controls --------------------------------------------------------------------------------

class ControlPage final : public SettingsPage {
public:
    explicit ControlPage(Frontend& fe) : SettingsPage(fe, "jpg/ctrl_bk.jpg") {
        Context& ctx = fe.ctx;
        menu.defaultHelp = "jpg/opt_tctl.jpg";
        menu.add<ui::ValueBox>(
            Box{kBoxX, 71, kBoxWide, kBoxH}, [&ctx] { return devices(ctx); },
            [&ctx] {
                const auto list = devices(ctx);
                const std::string cur = ctx.settings.ini.getString("Controls", "Device", "Keyboard");
                for (std::size_t i = 0; i < list.size(); ++i)
                    if (list[i] == cur)
                        return static_cast<int>(i);
                return 0;
            },
            [&ctx](int i) { ctx.settings.ini.set("Controls", "Device", devices(ctx)[static_cast<std::size_t>(i)]); });
        addSlider(ctx, Box{471, 105, 139, 34}, "SteeringSensitivity", 0.5f);
        addSlider(ctx, Box{471, 139, 139, 34}, "DeadZone", 0.1f);
        addSlider(ctx, Box{471, 242, 139, 33}, "CollisionIntensity", 0.5f);
        addSlider(ctx, Box{471, 275, 139, 33}, "RoadForceIntensity", 0.5f);
        addToggle(ctx, "texture/ctrl_aut.tga", 64, "AutoReverse");
        addToggle(ctx, "texture/ctrl_pov.tga", 91, "UsePovHat");
        addToggle(ctx, "texture/ctrl_fbk.tga", 118, "ForceFeedback");
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/ctrl_cal.tga", 4}, 460, 180, [&fe] {
            fe.message("Calibration is handled by the operating system; use the dead zone setting to adjust.");
        });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/ctrl_cus.tga", 4}, 346, 314,
                                   [&fe] { fe.push(makeCustomizeControlsPage(fe)); });
        addDefaults(fe, 345, [&ctx] {
            for (const auto& k : ctx.settings.ini.keys("Controls"))
                if (!k.starts_with("Bind."))
                    ctx.settings.ini.remove("Controls", k);
        });
    }

private:
    static std::vector<std::string> devices(Context& ctx) {
        const auto& s = ctx.game->strings;
        std::vector<std::string> v = {s.get(581, "Keyboard"), s.get(580, "Mouse")};
        for (const auto& pad : ctx.input.gamepads())
            v.push_back(pad.name);
        for (const auto& js : ctx.input.joysticks())
            v.push_back(js.name);
        return v;
    }
    void addSlider(Context& ctx, Box b, const char* key, float def) {
        menu.add<ui::Slider>(b, [&ctx, key, def] { return iniFloat(ctx, "Controls", key, def); },
                             [&ctx, key](float v) { ctx.settings.ini.setDouble("Controls", key, v); });
    }
    void addToggle(Context& ctx, const char* sprite, float y, const char* key) {
        menu.add<ui::LampItem>(
            SpriteSheet{sprite, 5}, kLampX, y, [&ctx, key] { return iniBool(ctx, "Controls", key, true); },
            [&ctx, key] { ctx.settings.ini.setBool("Controls", key, !iniBool(ctx, "Controls", key, true)); });
    }
};

// --- Customize controls ----------------------------------------------------------------------

// Default keyboard bindings for the actions of string ids 276-309 (inferred
// from common MM2 defaults; stored as [Controls] Bind.<id> = <key name>).
platform::Key defaultKey(std::uint32_t id) {
    using platform::Key;
    switch (id) {
    case 276: return Key::C;       // Change Camera
    case 278: return Key::T;       // Transmission
    case 279: return Key::H;       // Horn
    case 280: return Key::Up;      // Throttle
    case 281: return Key::Down;    // Brakes
    case 283: return Key::Left;    // Steer Left
    case 284: return Key::Right;   // Steer Right
    case 285: return Key::X;       // Look Right
    case 286: return Key::Z;       // Look Left
    case 287: return Key::B;       // Look Back
    case 290: return Key::D;       // Dashboard On/Off
    case 291: return Key::A;       // Shift Up
    case 292: return Key::Q;       // Shift Down
    case 293: return Key::R;       // Reverse
    case 296: return Key::M;       // Map Toggle
    case 297: return Key::F2;      // HUD Toggle
    case 298: return Key::F3;      // Full Screen Map
    case 299: return Key::Equals;  // Map Zoom
    case 305: return Key::F4;      // Rear View Mirror
    case 307: return Key::Space;   // Handbrake
    case 308: return Key::O;       // Opponent Position
    case 309: return Key::Return;  // Enter Chat Msg
    default: return Key::Unknown;
    }
}

class CustomizePage final : public Page {
public:
    explicit CustomizePage(Frontend& fe) {
        Context& ctx = fe.ctx;
        menu.background = "jpg/cuss_bk.jpg";
        for (std::uint32_t id = 276; id <= 309; ++id) {
            if (id == 277 || id == 282 || id == 288 || id == 289 || (id >= 300 && id <= 304) || id == 306)
                continue; // axes and CD-player actions are not bindable keys here
            m_actions.push_back(id);
        }
        auto& list = menu.add<ui::ListBox>(
            Box{36, 56, 306, 313},
            [this, &ctx] {
                std::vector<std::string> v;
                for (auto id : m_actions) {
                    const std::string key = id == m_capturing ? "Press a key..." : platform::keyName(binding(ctx, id));
                    v.push_back(std::format("{}  -  {}", ctx.game->strings.get(id), key));
                }
                return v;
            },
            [this] { return m_selected; }, [this](int i) { m_selected = i; });
        list.rowHeight = 17;
        list.onDoubleClick = [this] { m_capturing = m_actions[static_cast<std::size_t>(m_selected)]; };
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_def.tga", 4}, 33, 412, [&ctx] {
            for (const auto& k : ctx.settings.ini.keys("Controls"))
                if (k.starts_with("Bind."))
                    ctx.settings.ini.remove("Controls", k);
        });
        menu.add<ui::SpriteButton>(SpriteSheet{"texture/opt_done.tga", 4}, kNext.x, kNext.y, [&fe] {
            fe.ctx.saveSettings();
            fe.pop();
        });
        // Escape while waiting for a key only cancels the capture.
        menu.onBack = [this, &fe] {
            if (!m_swallowBack)
                fe.pop();
        };
        menu.focus(&list);
    }

    void update(Frontend& fe, double) override {
        auto& in = fe.ctx.input;
        m_swallowBack = m_capturing != 0;
        if (m_capturing) {
            for (auto k : in.keysPressedThisFrame()) {
                if (k != platform::Key::Escape)
                    fe.ctx.settings.ini.set("Controls", std::format("Bind.{}", m_capturing), platform::keyName(k));
                m_capturing = 0;
                in.beginFrame(); // swallow the key so the menu does not act on it
                return;
            }
        } else if (in.keyPressed(platform::Key::Return) && m_selected >= 0) {
            m_capturing = m_actions[static_cast<std::size_t>(m_selected)];
            in.beginFrame();
        }
    }

private:
    static platform::Key binding(Context& ctx, std::uint32_t id) {
        const std::string name = ctx.settings.ini.getString("Controls", std::format("Bind.{}", id));
        if (!name.empty())
            return platform::keyFromName(name);
        return defaultKey(id);
    }

    std::vector<std::uint32_t> m_actions;
    int m_selected = 0;
    std::uint32_t m_capturing = 0;
    bool m_swallowBack = false;
};

// --- About ------------------------------------------------------------------------------------

// The credits picture (credits.jpg, 215x4582) scrolls in the large black box
// under "Product ID:". OpenMM2 never reads CD keys, so no product ID is shown.
class AboutPage final : public Page {
public:
    explicit AboutPage(Frontend& fe) {
        menu.background = "jpg/about_bk.jpg";
        addBack(fe, *this);
    }
    void update(Frontend&, double dt) override { m_scroll += static_cast<float>(dt) * 30.0f; }
    void drawAbove(Frontend& fe, ui::UiFrame& f) override {
        const ui::UiTexture& t = fe.textures.get("jpg/credits.jpg");
        if (!t)
            return;
        const Box box{37, 199, 225, 178};
        const float h = static_cast<float>(t.height);
        const float offset = std::fmod(m_scroll, h + box.h) - box.h; // starts below the box
        const Vec4 clip{box.x, box.y, box.w, box.h};
        f.overlay.setClip(&clip);
        ui::drawImage(f.overlay, t, box.x + (box.w - static_cast<float>(t.width)) * 0.5f, box.y - offset);
        f.overlay.setClip(nullptr);
    }

private:
    float m_scroll = 0.0f;
};

} // namespace

std::unique_ptr<Page> makeOptionsPage(Frontend& fe) { return std::make_unique<OptionsPage>(fe); }
std::unique_ptr<Page> makeGraphicsPage(Frontend& fe) { return std::make_unique<GraphicsPage>(fe); }
std::unique_ptr<Page> makeAudioPage(Frontend& fe) { return std::make_unique<AudioPage>(fe); }
std::unique_ptr<Page> makeControlPage(Frontend& fe) { return std::make_unique<ControlPage>(fe); }
std::unique_ptr<Page> makeCustomizeControlsPage(Frontend& fe) { return std::make_unique<CustomizePage>(fe); }
std::unique_ptr<Page> makeAboutPage(Frontend& fe) { return std::make_unique<AboutPage>(fe); }

} // namespace mm2::app::frontend
