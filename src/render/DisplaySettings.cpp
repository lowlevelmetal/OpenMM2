#include "render/DisplaySettings.h"

#include "core/Ini.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace mm2::render {
namespace {

template <class E, std::size_t N>
bool parseEnum(std::string_view text, E& out, const std::pair<E, const char*> (&table)[N]) {
    for (const auto& [value, name] : table) {
        if (str::iequals(text, name)) {
            out = value;
            return true;
        }
    }
    return false;
}

template <class E, std::size_t N>
const char* enumName(E value, const std::pair<E, const char*> (&table)[N]) {
    for (const auto& [v, name] : table)
        if (v == value)
            return name;
    return table[0].second;
}

constexpr std::pair<Backend, const char*> kBackends[] = {
    {Backend::Auto, "auto"}, {Backend::Vulkan, "vulkan"}, {Backend::OpenGL, "opengl"}};
constexpr std::pair<VsyncMode, const char*> kVsync[] = {
    {VsyncMode::On, "on"}, {VsyncMode::Off, "off"}, {VsyncMode::Adaptive, "adaptive"}, {VsyncMode::Mailbox, "mailbox"}};
constexpr std::pair<UiScaleMode, const char*> kUiScale[] = {
    {UiScaleMode::Fit, "fit"}, {UiScaleMode::Stretch, "stretch"}, {UiScaleMode::Integer, "integer"}};
constexpr std::pair<FovMode, const char*> kFov[] = {
    {FovMode::HorPlus, "hor+"}, {FovMode::VertMinus, "vert-"}, {FovMode::Stretch, "stretch"}};

} // namespace

const char* backendName(Backend b) { return enumName(b, kBackends); }
const char* vsyncModeName(VsyncMode m) { return enumName(m, kVsync); }
const char* uiScaleModeName(UiScaleMode m) { return enumName(m, kUiScale); }
const char* fovModeName(FovMode m) { return enumName(m, kFov); }
bool parseBackend(std::string_view text, Backend& out) {
    if (str::iequals(text, "gl")) {
        out = Backend::OpenGL;
        return true;
    }
    if (str::iequals(text, "vk")) {
        out = Backend::Vulkan;
        return true;
    }
    return parseEnum(text, out, kBackends);
}
bool parseVsyncMode(std::string_view text, VsyncMode& out) {
    if (auto b = str::parseBool(text)) {
        out = *b ? VsyncMode::On : VsyncMode::Off;
        return true;
    }
    return parseEnum(text, out, kVsync);
}
bool parseUiScaleMode(std::string_view text, UiScaleMode& out) { return parseEnum(text, out, kUiScale); }
bool parseFovMode(std::string_view text, FovMode& out) { return parseEnum(text, out, kFov); }

void DisplaySettings::load(const IniFile& ini, std::string_view s) {
    if (auto v = ini.get(s, "Backend"))
        parseBackend(*v, backend);
    if (auto v = ini.get(s, "Mode"))
        platform::parseWindowMode(*v, windowMode);
    display = static_cast<int>(ini.getInt(s, "Display", display));
    windowWidth = static_cast<int>(ini.getInt(s, "WindowWidth", windowWidth));
    windowHeight = static_cast<int>(ini.getInt(s, "WindowHeight", windowHeight));
    fullscreenMode.width = static_cast<int>(ini.getInt(s, "FullscreenWidth", fullscreenMode.width));
    fullscreenMode.height = static_cast<int>(ini.getInt(s, "FullscreenHeight", fullscreenMode.height));
    fullscreenMode.refreshRate = static_cast<float>(ini.getDouble(s, "RefreshRate", fullscreenMode.refreshRate));
    if (auto v = ini.get(s, "VSync"))
        parseVsyncMode(*v, vsync);
    frameCap = static_cast<int>(ini.getInt(s, "FrameCap", frameCap));
    msaa = static_cast<std::uint32_t>(ini.getInt(s, "MSAA", msaa));
    anisotropy = static_cast<std::uint32_t>(ini.getInt(s, "Anisotropy", anisotropy));
    renderScale = static_cast<float>(ini.getDouble(s, "RenderScale", renderScale));
    if (auto v = ini.get(s, "UIScale"))
        parseUiScaleMode(*v, uiScale);
    if (auto v = ini.get(s, "FOV"))
        parseFovMode(*v, fovMode);
    maxAspect = static_cast<float>(ini.getDouble(s, "MaxAspect", maxAspect));
    gpu = static_cast<int>(ini.getInt(s, "GPU", gpu));
    validation = ini.getBool(s, "Validation", validation);
    sanitize();
}

void DisplaySettings::save(IniFile& ini, std::string_view s) const {
    ini.set(s, "Backend", backendName(backend));
    ini.set(s, "Mode", platform::windowModeName(windowMode));
    ini.setInt(s, "Display", display);
    ini.setInt(s, "WindowWidth", windowWidth);
    ini.setInt(s, "WindowHeight", windowHeight);
    ini.setInt(s, "FullscreenWidth", fullscreenMode.width);
    ini.setInt(s, "FullscreenHeight", fullscreenMode.height);
    ini.set(s, "RefreshRate", std::format("{:g}", fullscreenMode.refreshRate));
    ini.set(s, "VSync", vsyncModeName(vsync));
    ini.setInt(s, "FrameCap", frameCap);
    ini.setInt(s, "MSAA", msaa);
    ini.setInt(s, "Anisotropy", anisotropy);
    ini.set(s, "RenderScale", std::format("{:g}", renderScale));
    ini.set(s, "UIScale", uiScaleModeName(uiScale));
    ini.set(s, "FOV", fovModeName(fovMode));
    ini.set(s, "MaxAspect", std::format("{:g}", maxAspect));
    ini.setInt(s, "GPU", gpu);
    ini.setBool(s, "Validation", validation);
}

void DisplaySettings::sanitize() {
    display = std::max(display, 0);
    windowWidth = std::clamp(windowWidth, 320, 16384);
    windowHeight = std::clamp(windowHeight, 240, 16384);
    fullscreenMode.width = std::max(fullscreenMode.width, 0);
    fullscreenMode.height = std::max(fullscreenMode.height, 0);
    frameCap = std::clamp(frameCap, 0, 1000);
    if (frameCap > 0 && frameCap < 10)
        frameCap = 10;
    // Round MSAA down to a power of two in 1..8.
    std::uint32_t m = 1;
    while (m * 2 <= std::min<std::uint32_t>(msaa, 8))
        m *= 2;
    msaa = m;
    anisotropy = std::clamp<std::uint32_t>(anisotropy, 1, 16);
    renderScale = std::clamp(renderScale, 0.25f, 2.0f);
    if (maxAspect < 0.0f || (maxAspect > 0.0f && maxAspect < 4.0f / 3.0f))
        maxAspect = 0.0f;
}

} // namespace mm2::render
