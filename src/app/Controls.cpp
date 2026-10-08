#include "app/Controls.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mm2::app::controls {
namespace {

using platform::Key;

// mmInput::SetDefaultConfig, keyboard device (1): DirectInput key codes in
// slot order (TAB, Q, E, F, H, -, LEFT, RIGHT, UP, DOWN, SPACE, C, V,
// RETURN, NUMPAD4/6/2/8, W, D, T, A, Z, R, S, X, 2, 3, 4, 5, BACKSPACE, -,
// I, Y).
constexpr std::array<ActionInfo, static_cast<std::size_t>(Action::Count)> kActions = {{
    {296, Key::Tab, true},       // Map Toggle
    {298, Key::Q, true},         // Full Screen Map
    {299, Key::E, true},         // Map Zoom
    {300, Key::F, true},         // Rotating Map
    {297, Key::H, true},         // HUD Toggle
    {282, Key::Unknown, false},  // Steering
    {283, Key::Left, true},      // Steer Left
    {284, Key::Right, true},     // Steer Right
    {280, Key::Up, true},        // Throttle
    {281, Key::Down, true},      // Brakes
    {307, Key::Space, true},     // Handbrake
    {276, Key::C, true},         // Change Camera
    {277, Key::V, true},         // Thrill Cam
    {279, Key::Return, true},    // Horn
    {286, Key::Kp4, true},       // Look Left
    {285, Key::Kp6, true},       // Look Right
    {287, Key::Kp2, true},       // Look Back
    {288, Key::Kp8, true},       // Look Forward
    {289, Key::W, true},         // Wide Angle
    {290, Key::D, true},         // Dashboard On/Off
    {278, Key::T, true},         // Transmission
    {291, Key::A, true},         // Shift Up
    {292, Key::Z, true},         // Shift Down
    {293, Key::R, true},         // Reverse
    {294, Key::S, true},         // Next Checkpoint
    {295, Key::X, true},         // Prev. Checkpoint
    {301, Key::Num2, true},      // Toggle CD Player
    {302, Key::Num3, true},      // Start/Stop CD
    {304, Key::Num4, true},      // Prev. CD Track
    {303, Key::Num5, true},      // Next CD Track
    {305, Key::Backspace, true}, // Rear View Mirror
    {306, Key::Unknown, false},  // Camera Pan
    {308, Key::I, true},         // Opponent Position
    {309, Key::Y, true},         // Enter Chat Msg
}};

float sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

} // namespace

const std::array<ActionInfo, static_cast<std::size_t>(Action::Count)>& actions() { return kActions; }

const ActionInfo& info(Action a) { return kActions[static_cast<std::size_t>(a)]; }

std::string bindKey(std::uint32_t stringId) { return std::format("Bind.{}", stringId); }

Key boundKey(const IniFile& ini, const ActionInfo& a) {
    const std::string name = ini.getString("Controls", bindKey(a.stringId));
    if (name.empty())
        return a.key;
    if (name == kUnbound)
        return Key::Unknown;
    return platform::keyFromName(name);
}

void Bindings::load(const IniFile& ini) {
    for (std::size_t i = 0; i < kActions.size(); ++i)
        m_keys[i] = boundKey(ini, kActions[i]);
}

bool Bindings::down(const platform::Input& in, Action a) const {
    const Key k = key(a);
    return k != Key::Unknown && in.keyDown(k);
}

bool Bindings::pressed(const platform::Input& in, Action a) const {
    const Key k = key(a);
    return k != Key::Unknown && in.keyPressed(k);
}

Options Options::load(const IniFile& ini) {
    Options o;
    o.autoReverse = ini.getBool("Controls", "AutoReverse", o.autoReverse);
    o.deadZone = std::clamp(static_cast<float>(ini.getDouble("Controls", "DeadZone", o.deadZone)), 0.0f, 0.33f);
    o.sensitivity =
        std::clamp(static_cast<float>(ini.getDouble("Controls", "Sensitivity", o.sensitivity)), 0.5f, 2.0f);
    o.usePovHat = ini.getBool("Controls", "UsePovHat", o.usePovHat);
    o.forceFeedback = ini.getBool("Controls", "ForceFeedback", o.forceFeedback);
    return o;
}

float applyDeadZone(float value, float deadZone) {
    const float a = std::abs(value);
    if (a <= deadZone || deadZone >= 1.0f)
        return 0.0f;
    return sign(value) * std::min(1.0f, (a - deadZone) / (1.0f - deadZone));
}

} // namespace mm2::app::controls
