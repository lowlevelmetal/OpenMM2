#pragma once

#include <cstdint>
#include <functional>
#include <string>

union SDL_Event;

namespace mm2::platform {

class Input;

struct InitOptions {
    std::string appName = "OpenMM2";
    std::string appVersion;
    std::string appIdentifier = "io.github.lowlevelmetal.OpenMM2";
    bool video = true;   // windows, displays, dialogs
    bool gamepad = true; // gamepads, joysticks, haptics
};

// Initialises SDL. Must be called on the main thread before any other
// platform or render function. Returns false (and sets `error`) on failure.
bool init(const InitOptions& options = {}, std::string* error = nullptr);
void shutdown();
bool initialized();

// Last SDL error message.
std::string lastError();

// What happened during one pollEvents() call.
struct EventSummary {
    bool quitRequested = false;  // window closed / OS quit request
    bool resized = false;        // drawable size changed (recreate swapchain)
    bool focusLost = false;
    bool focusGained = false;
    bool minimized = false;
    bool restored = false;
    bool displayChanged = false; // window moved to another display or displays changed
};

// Drains the OS event queue. Every event is first offered to `hook` (for
// Dear ImGui); then it updates `input` (if non-null). Completed file-dialog
// callbacks are dispatched from here, on the calling (main) thread.
// Call Input::beginFrame() before this each frame.
EventSummary pollEvents(Input* input, const std::function<void(const SDL_Event&)>& hook = {});

// Blocks until an OS event is pending or `timeoutSeconds` passed, without
// removing the event (the next pollEvents() handles it).
void waitForEvents(double timeoutSeconds);

// Monotonic high-resolution time.
std::uint64_t nowNs();
inline double nowSeconds() { return static_cast<double>(nowNs()) * 1e-9; }

// Sleeps with sub-millisecond accuracy (sleeps most of the time, spins the end).
void sleepPrecise(double seconds);

} // namespace mm2::platform
