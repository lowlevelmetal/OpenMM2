#include "render/Renderer.h"

#include "core/Log.h"

#include <format>

namespace mm2::render {

FrameConstants overlayConstants(Extent2D output) {
    FrameConstants fc;
    fc.proj = Mat44::orthographic(0.0f, static_cast<float>(std::max<std::uint32_t>(output.width, 1)),
                                  static_cast<float>(std::max<std::uint32_t>(output.height, 1)), 0.0f, -1.0f, 1.0f,
                                  true);
    return fc;
}

namespace {

platform::WindowDesc windowDescFor(const RendererConfig& config, platform::GraphicsApi api) {
    const DisplaySettings& s = config.settings;
    platform::WindowDesc d;
    d.title = config.title;
    d.api = api;
    d.mode = s.windowMode;
    d.display = s.display;
    d.width = s.windowWidth;
    d.height = s.windowHeight;
    d.fullscreenMode = s.fullscreenMode;
    d.hidden = config.hiddenWindow;
    d.glDebug = s.validation;
    return d;
}

std::optional<Renderer> tryBackend(const RendererConfig& config, Backend backend, std::string* error) {
    const auto api = backend == Backend::Vulkan ? platform::GraphicsApi::Vulkan : platform::GraphicsApi::OpenGL;
    Renderer r;
    std::string err;
    r.window = platform::Window::create(windowDescFor(config, api), &err);
    if (!r.window) {
        *error = std::format("cannot create {} window: {}", backendName(backend), err);
        return std::nullopt;
    }
    DeviceCreateInfo info;
    info.window = r.window.get();
    info.settings = config.settings;
    info.pipelineCachePath = config.pipelineCachePath;
    switch (backend) {
#ifdef OPENMM2_HAS_VULKAN
    case Backend::Vulkan: r.device = createVulkanDevice(info, &err); break;
#endif
#ifdef OPENMM2_HAS_OPENGL
    case Backend::OpenGL: r.device = createOpenGLDevice(info, &err); break;
#endif
    default: err = "backend not compiled in"; break;
    }
    if (!r.device) {
        *error = std::format("{}: {}", backendName(backend), err);
        return std::nullopt;
    }
    return r;
}

} // namespace

std::optional<Renderer> createRenderer(const RendererConfig& config, std::string* error) {
    std::vector<Backend> order;
    switch (config.settings.backend) {
    case Backend::OpenGL: order = {Backend::OpenGL, Backend::Vulkan}; break;
    case Backend::Vulkan:
    case Backend::Auto: order = {Backend::Vulkan, Backend::OpenGL}; break;
    }
#ifndef OPENMM2_HAS_VULKAN
    std::erase(order, Backend::Vulkan);
#endif
#ifndef OPENMM2_HAS_OPENGL
    std::erase(order, Backend::OpenGL);
#endif

    std::string reasons;
    for (Backend b : order) {
        std::string err;
        if (auto r = tryBackend(config, b, &err)) {
            r->fallbackReason = reasons;
            if (!reasons.empty())
                log::warn("render: using {} ({})", backendName(b), reasons);
            const DeviceInfo& di = r->device->info();
            log::info("render: {} on {} ({})", di.apiVersion, di.deviceName, di.driverInfo);
            return r;
        }
        log::warn("render: {}", err);
        if (!reasons.empty())
            reasons += "; ";
        reasons += err;
    }
    if (error)
        *error = reasons.empty() ? "no rendering backend available" : reasons;
    return std::nullopt;
}

bool applyDisplaySettings(Renderer& r, const DisplaySettings& cur, const DisplaySettings& next) {
    const bool backendChanged = next.backend != cur.backend && next.backend != Backend::Auto &&
                                next.backend != r.device->backend();
    if (backendChanged || next.gpu != cur.gpu || next.validation != cur.validation)
        return false;

    if (next.windowMode != cur.windowMode || next.display != cur.display || next.fullscreenMode != cur.fullscreenMode ||
        (next.windowMode == platform::WindowMode::Windowed &&
         (next.windowWidth != cur.windowWidth || next.windowHeight != cur.windowHeight))) {
        r.window->applyMode(next.windowMode, next.display, {next.windowWidth, next.windowHeight}, next.fullscreenMode);
        r.device->notifyResized();
    }
    r.device->applySettings(next);
    return true;
}

} // namespace mm2::render
