#pragma once

#include "platform/Window.h"
#include "render/Types.h"

#include <string_view>

namespace mm2 {
class IniFile;
}

namespace mm2::render {

enum class VsyncMode : std::uint8_t {
    Off,      // present immediately (may tear)
    On,       // wait for vertical blank (FIFO)
    Adaptive, // vsync, but tear instead of stalling when a frame is late
    Mailbox,  // no tearing, lowest latency; falls back to On if unsupported
};

// How the original 640x480 user interface is fitted to the window.
enum class UiScaleMode : std::uint8_t {
    Fit,     // uniform scale, centred, letterbox/pillarbox bars
    Stretch, // fill the window (distorts on non-4:3 screens)
    Integer, // largest whole-number scale that fits (pixel-exact)
};

// How the field of view adapts to aspect ratios other than the original 4:3.
enum class FovMode : std::uint8_t {
    HorPlus,   // keep the 4:3 vertical FOV, widen horizontally (default)
    VertMinus, // keep the original horizontal FOV, crop vertically
    Stretch,   // render 4:3 and stretch to the window (original look on widescreen)
};

// Every display option the game persists. Stored in the [Display] section
// of the user configuration.
struct DisplaySettings {
    Backend backend = Backend::Auto;
    platform::WindowMode windowMode = platform::WindowMode::Windowed;
    int display = 0;                     // index into platform::enumerateDisplays()
    int windowWidth = 1280;              // windowed size in points
    int windowHeight = 960;
    platform::DisplayMode fullscreenMode; // exclusive fullscreen mode; 0x0 = desktop
    VsyncMode vsync = VsyncMode::On;
    int frameCap = 0;                     // fps limit, 0 = unlimited
    std::uint32_t msaa = 4;               // 1, 2, 4, 8
    std::uint32_t anisotropy = 8;         // 1 = off
    float renderScale = 1.0f;             // 3D resolution relative to the window (0.25 .. 2)
    UiScaleMode uiScale = UiScaleMode::Fit;
    FovMode fovMode = FovMode::HorPlus;
    float maxAspect = 0.0f;               // Hor+ limit for ultrawide (e.g. 2.4); 0 = none
    int gpu = -1;                         // Vulkan physical device index, -1 = automatic
    bool validation = false;              // graphics API debug layers

    void load(const IniFile& ini, std::string_view section = "Display");
    void save(IniFile& ini, std::string_view section = "Display") const;
    // Clamps values into supported ranges.
    void sanitize();
};

const char* vsyncModeName(VsyncMode m);
const char* uiScaleModeName(UiScaleMode m);
const char* fovModeName(FovMode m);
bool parseBackend(std::string_view text, Backend& out);
bool parseVsyncMode(std::string_view text, VsyncMode& out);
bool parseUiScaleMode(std::string_view text, UiScaleMode& out);
bool parseFovMode(std::string_view text, FovMode& out);

} // namespace mm2::render
