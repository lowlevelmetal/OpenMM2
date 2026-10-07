#pragma once

union SDL_Event;

namespace mm2::platform {

class Window;

// Dear ImGui platform glue (SDL3 backend): input, cursor, clipboard, IME.
// The renderer side lives in render/ImGuiRenderer. The ImGui context must be
// created by the caller before init().
namespace imgui {

bool init(Window& window);
void shutdown();
// Call once per frame before ImGui::NewFrame().
void newFrame();
// Feed every SDL event (pass as the pollEvents() hook). Returns true if
// ImGui consumed it.
bool processEvent(const SDL_Event& event);
// Whether ImGui currently wants exclusive keyboard / mouse input.
bool wantsKeyboard();
bool wantsMouse();

} // namespace imgui
} // namespace mm2::platform
