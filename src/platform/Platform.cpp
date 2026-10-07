#include "platform/Platform.h"

#include "core/Log.h"
#include "platform/Dialogs.h"
#include "platform/Input.h"

#include <SDL3/SDL.h>

namespace mm2::platform {
namespace {

bool g_initialized = false;

} // namespace

bool init(const InitOptions& options, std::string* error) {
    if (g_initialized)
        return true;
    SDL_SetAppMetadata(options.appName.c_str(), options.appVersion.empty() ? nullptr : options.appVersion.c_str(),
                       options.appIdentifier.c_str());
    // Keep the desktop compositor running in fullscreen: modern Wayland/X11
    // compositors handle fullscreen unredirection themselves.
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    // Gamepads keep reporting while the window is unfocused only if asked to;
    // a racing game should not steer while alt-tabbed.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

    SDL_InitFlags flags = SDL_INIT_EVENTS;
    if (options.video)
        flags |= SDL_INIT_VIDEO;
    if (options.gamepad)
        flags |= SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC;
    if (!SDL_Init(flags)) {
        // Haptics are optional; retry without them before giving up.
        if (options.gamepad && SDL_Init(flags & ~SDL_INIT_HAPTIC)) {
            log::warn("platform: haptics unavailable: {}", SDL_GetError());
        } else {
            if (error)
                *error = SDL_GetError();
            return false;
        }
    }
    g_initialized = true;
    if (options.video)
        log::info("platform: SDL {}.{}.{}, video driver '{}'", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
                  SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()),
                  SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    return true;
}

void shutdown() {
    if (!g_initialized)
        return;
    SDL_Quit();
    g_initialized = false;
}

bool initialized() { return g_initialized; }

std::string lastError() { return SDL_GetError(); }

EventSummary pollEvents(Input* input, const std::function<void(const SDL_Event&)>& hook) {
    EventSummary summary;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (hook)
            hook(ev);
        if (input)
            input->handleEvent(ev);
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: summary.quitRequested = true; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: summary.resized = true; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: summary.focusLost = true; break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: summary.focusGained = true; break;
        case SDL_EVENT_WINDOW_MINIMIZED: summary.minimized = true; break;
        case SDL_EVENT_WINDOW_RESTORED: summary.restored = true; break;
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED: summary.displayChanged = true; break;
        default: break;
        }
    }
    if (input)
        input->updateDevices();
    dispatchDialogResults();
    return summary;
}

std::uint64_t nowNs() { return SDL_GetTicksNS(); }

void sleepPrecise(double seconds) {
    if (seconds <= 0.0)
        return;
    SDL_DelayPrecise(static_cast<Uint64>(seconds * 1e9));
}

} // namespace mm2::platform
