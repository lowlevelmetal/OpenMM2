#include "platform/ImGuiPlatform.h"

#include "platform/Window.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace mm2::platform::imgui {

bool init(Window& window) {
    // The renderer backend is our own RHI for both APIs, so the platform side
    // needs no API-specific setup.
    return ImGui_ImplSDL3_InitForOther(window.sdl());
}

void shutdown() { ImGui_ImplSDL3_Shutdown(); }

void newFrame() { ImGui_ImplSDL3_NewFrame(); }

bool processEvent(const SDL_Event& event) { return ImGui_ImplSDL3_ProcessEvent(&event); }

bool wantsKeyboard() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard; }

bool wantsMouse() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse; }

} // namespace mm2::platform::imgui
