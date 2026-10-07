#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace mm2::platform {

enum class GraphicsApi { Vulkan, OpenGL };

enum class WindowMode {
    Windowed,   // decorated, resizable window
    Borderless, // fullscreen at desktop resolution ("fullscreen windowed")
    Fullscreen, // exclusive fullscreen at a chosen display mode
};

const char* windowModeName(WindowMode mode);
bool parseWindowMode(std::string_view text, WindowMode& out);

struct Extent {
    int width = 0;
    int height = 0;
    bool operator==(const Extent&) const = default;
    bool empty() const { return width <= 0 || height <= 0; }
};

struct DisplayMode {
    int width = 0;
    int height = 0;
    float refreshRate = 0.0f;  // Hz, 0 if unknown
    float pixelDensity = 1.0f; // >1 for HiDPI modes
    bool operator==(const DisplayMode&) const = default;
};

struct DisplayInfo {
    std::uint32_t id = 0; // SDL display id (changes across hotplug)
    std::string name;
    int x = 0, y = 0, width = 0, height = 0; // desktop bounds, in points
    float contentScale = 1.0f;               // OS UI scale (e.g. 1.5 at 150%)
    DisplayMode desktop;
    std::vector<DisplayMode> modes; // fullscreen modes, largest first
};

// Connected displays, primary first.
std::vector<DisplayInfo> enumerateDisplays();

struct WindowDesc {
    std::string title = "OpenMM2";
    GraphicsApi api = GraphicsApi::Vulkan;
    WindowMode mode = WindowMode::Windowed;
    int display = 0;  // index into enumerateDisplays()
    int width = 1280; // windowed size in points
    int height = 720;
    DisplayMode fullscreenMode; // for WindowMode::Fullscreen; 0x0 = desktop mode
    bool resizable = true;
    bool hidden = false; // create hidden (offscreen tests); show() later
    bool highDpi = true; // render at native pixel density on HiDPI displays
    // OpenGL context attributes (only used with GraphicsApi::OpenGL).
    int glMajor = 3;
    int glMinor = 3;
    bool glDebug = false;
};

class Window {
public:
    static std::unique_ptr<Window> create(const WindowDesc& desc, std::string* error = nullptr);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SDL_Window* sdl() const { return m_window; }
    GraphicsApi api() const { return m_api; }

    void setTitle(const std::string& title);
    void show();

    // Switches window mode / size / display. For Fullscreen the closest mode
    // supported by the display is used. Returns false if the OS refused.
    bool applyMode(WindowMode mode, int display, Extent windowedSize, const DisplayMode& fullscreenMode);
    WindowMode mode() const { return m_mode; }

    Extent size() const;      // in points (logical units)
    Extent pixelSize() const; // drawable size in pixels
    float displayScale() const;  // content scale (points->UI scale factor, e.g. 2.0 on HiDPI)
    float pixelDensity() const;  // pixels per point
    int displayIndex() const;    // index into enumerateDisplays()
    bool minimized() const;
    bool focused() const;

    // Fullscreen mode actually in use, if exclusive fullscreen.
    DisplayMode currentFullscreenMode() const;

private:
    Window() = default;
    SDL_Window* m_window = nullptr;
    GraphicsApi m_api = GraphicsApi::Vulkan;
    WindowMode m_mode = WindowMode::Windowed;
};

} // namespace mm2::platform
