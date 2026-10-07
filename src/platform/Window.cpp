#include "platform/Window.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace mm2::platform {
namespace {

DisplayMode toMode(const SDL_DisplayMode& m) {
    return {m.w, m.h, m.refresh_rate, m.pixel_density > 0 ? m.pixel_density : 1.0f};
}

// SDL display ids ordered with the primary display first.
std::vector<SDL_DisplayID> orderedDisplayIds() {
    std::vector<SDL_DisplayID> ids;
    int count = 0;
    if (SDL_DisplayID* list = SDL_GetDisplays(&count)) {
        ids.assign(list, list + count);
        SDL_free(list);
    }
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    std::ranges::stable_partition(ids, [primary](SDL_DisplayID id) { return id == primary; });
    return ids;
}

SDL_DisplayID displayIdAt(int index) {
    const auto ids = orderedDisplayIds();
    if (ids.empty())
        return 0;
    return ids[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(ids.size()) - 1))];
}

} // namespace

const char* windowModeName(WindowMode mode) {
    switch (mode) {
    case WindowMode::Windowed: return "windowed";
    case WindowMode::Borderless: return "borderless";
    case WindowMode::Fullscreen: return "fullscreen";
    }
    return "windowed";
}

bool parseWindowMode(std::string_view text, WindowMode& out) {
    for (WindowMode m : {WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen}) {
        if (str::iequals(text, windowModeName(m))) {
            out = m;
            return true;
        }
    }
    return false;
}

std::vector<DisplayInfo> enumerateDisplays() {
    std::vector<DisplayInfo> out;
    for (SDL_DisplayID id : orderedDisplayIds()) {
        DisplayInfo d;
        d.id = id;
        if (const char* name = SDL_GetDisplayName(id))
            d.name = name;
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(id, &bounds)) {
            d.x = bounds.x;
            d.y = bounds.y;
            d.width = bounds.w;
            d.height = bounds.h;
        }
        d.contentScale = SDL_GetDisplayContentScale(id);
        if (d.contentScale <= 0.0f)
            d.contentScale = 1.0f;
        if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(id))
            d.desktop = toMode(*desk);
        int count = 0;
        if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(id, &count)) {
            for (int i = 0; i < count; ++i) {
                const DisplayMode m = toMode(*modes[i]);
                if (std::ranges::find(d.modes, m) == d.modes.end())
                    d.modes.push_back(m);
            }
            SDL_free(modes);
        }
        // Wayland exposes no mode list (modes are emulated by scaling);
        // offer the desktop mode so exclusive fullscreen still works.
        if (d.modes.empty() && d.desktop.width > 0)
            d.modes.push_back(d.desktop);
        out.push_back(std::move(d));
    }
    return out;
}

std::unique_ptr<Window> Window::create(const WindowDesc& desc, std::string* error) {
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
    if (desc.resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.highDpi)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (desc.api == GraphicsApi::OpenGL) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desc.glMajor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desc.glMinor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        int ctxFlags = 0;
#ifdef __APPLE__
        ctxFlags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#endif
        if (desc.glDebug)
            ctxFlags |= SDL_GL_CONTEXT_DEBUG_FLAG;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, ctxFlags);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0); // opaque; avoids compositor blending
        // The 3D scene is drawn into offscreen targets; the window surface
        // only receives the composited image, so it needs no depth/MSAA.
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
    } else {
        flags |= SDL_WINDOW_VULKAN;
    }

    const SDL_DisplayID displayId = displayIdAt(desc.display);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc.title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, std::max(desc.width, 320));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, std::max(desc.height, 240));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    SDL_Window* sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!sdlWindow) {
        if (error)
            *error = SDL_GetError();
        return nullptr;
    }
    SDL_SetWindowMinimumSize(sdlWindow, 320, 240);

    std::unique_ptr<Window> w(new Window());
    w->m_window = sdlWindow;
    w->m_api = desc.api;
    w->applyMode(desc.mode, desc.display, {desc.width, desc.height}, desc.fullscreenMode);
    if (!desc.hidden)
        w->show();
    return w;
}

Window::~Window() {
    if (m_window)
        SDL_DestroyWindow(m_window);
}

void Window::setTitle(const std::string& title) { SDL_SetWindowTitle(m_window, title.c_str()); }

void Window::show() {
    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);
    SDL_SyncWindow(m_window);
}

bool Window::applyMode(WindowMode mode, int display, Extent windowedSize, const DisplayMode& fullscreenMode) {
    const SDL_DisplayID displayId = displayIdAt(display);
    bool ok = true;
    switch (mode) {
    case WindowMode::Windowed: {
        ok = SDL_SetWindowFullscreen(m_window, false);
        SDL_SyncWindow(m_window);
        SDL_SetWindowSize(m_window, std::max(windowedSize.width, 320), std::max(windowedSize.height, 240));
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId),
                              SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
        break;
    }
    case WindowMode::Borderless: {
        if (SDL_GetDisplayForWindow(m_window) != displayId)
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
        ok = SDL_SetWindowFullscreenMode(m_window, nullptr) && SDL_SetWindowFullscreen(m_window, true);
        break;
    }
    case WindowMode::Fullscreen: {
        if (SDL_GetDisplayForWindow(m_window) != displayId)
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED_DISPLAY(displayId),
                                  SDL_WINDOWPOS_CENTERED_DISPLAY(displayId));
        SDL_DisplayMode closest{};
        int w = fullscreenMode.width, h = fullscreenMode.height;
        float hz = fullscreenMode.refreshRate;
        if (w <= 0 || h <= 0) {
            if (const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(displayId)) {
                w = desk->w;
                h = desk->h;
                hz = desk->refresh_rate;
            }
        }
        if (SDL_GetClosestFullscreenDisplayMode(displayId, w, h, hz, true, &closest)) {
            ok = SDL_SetWindowFullscreenMode(m_window, &closest) && SDL_SetWindowFullscreen(m_window, true);
        } else {
            log::warn("platform: no fullscreen mode near {}x{}@{}; using borderless", w, h, hz);
            ok = SDL_SetWindowFullscreenMode(m_window, nullptr) && SDL_SetWindowFullscreen(m_window, true);
        }
        break;
    }
    }
    SDL_SyncWindow(m_window);
    if (!ok)
        log::warn("platform: switching to {} mode failed: {}", windowModeName(mode), SDL_GetError());
    m_mode = mode;
    return ok;
}

Extent Window::size() const {
    Extent e;
    SDL_GetWindowSize(m_window, &e.width, &e.height);
    return e;
}

Extent Window::pixelSize() const {
    Extent e;
    SDL_GetWindowSizeInPixels(m_window, &e.width, &e.height);
    return e;
}

float Window::displayScale() const {
    const float s = SDL_GetWindowDisplayScale(m_window);
    return s > 0.0f ? s : 1.0f;
}

float Window::pixelDensity() const {
    const float d = SDL_GetWindowPixelDensity(m_window);
    return d > 0.0f ? d : 1.0f;
}

int Window::displayIndex() const {
    const SDL_DisplayID id = SDL_GetDisplayForWindow(m_window);
    const auto ids = orderedDisplayIds();
    const auto it = std::ranges::find(ids, id);
    return it == ids.end() ? 0 : static_cast<int>(it - ids.begin());
}

bool Window::minimized() const { return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0; }

bool Window::focused() const { return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0; }

DisplayMode Window::currentFullscreenMode() const {
    if (const SDL_DisplayMode* m = SDL_GetWindowFullscreenMode(m_window))
        return toMode(*m);
    return {};
}

} // namespace mm2::platform
