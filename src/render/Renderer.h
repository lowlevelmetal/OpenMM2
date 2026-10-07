#pragma once

#include "platform/Window.h"
#include "render/Device.h"
#include "render/DisplaySettings.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace mm2::render {

struct RendererConfig {
    DisplaySettings settings;
    std::string title = "OpenMM2";
    std::filesystem::path pipelineCachePath;
    bool hiddenWindow = false; // keep the window hidden (automated tests)
};

// A window plus the device drawing into it. The device is destroyed first.
struct Renderer {
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<Device> device;
    std::string fallbackReason; // why Vulkan was skipped when Auto chose OpenGL
};

// Creates the window and device. Backend::Auto tries Vulkan and falls back
// to OpenGL if Vulkan is unavailable or fails to initialise. An explicitly
// requested backend that fails also falls back to the other one, so the game
// always starts if either works; `fallbackReason` explains what happened.
std::optional<Renderer> createRenderer(const RendererConfig& config, std::string* error = nullptr);

// Applies new display settings to a running renderer: window mode, size,
// display, vsync, MSAA, anisotropy, render scale. Returns false when the
// change requires a new renderer (backend or GPU change, validation toggle);
// the caller should then destroy and recreate it.
bool applyDisplaySettings(Renderer& renderer, const DisplaySettings& current, const DisplaySettings& next);

} // namespace mm2::render
