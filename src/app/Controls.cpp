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

void Bindings::load(const IniFile& ini) {
    for (std::size_t i = 0; i < kActions.size(); ++i) {
        const ActionInfo& a = kActions[i];
        const std::string name = ini.getString("Controls", bindKey(a.stringId));
        if (name.empty())
            m_keys[i] = a.key;
        else if (name == kUnbound)
            m_keys[i] = Key::Unknown;
        else
            m_keys[i] = platform::keyFromName(name);
    }
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
    o.controller = static_cast<Controller>(
        std::clamp(ini.getInt("Controls", "Controller", static_cast<int>(o.controller)), 0LL, 4LL));
    o.autoReverse = ini.getBool("Controls", "AutoReverse", o.autoReverse);
    o.deadZone = std::clamp(static_cast<float>(ini.getDouble("Controls", "DeadZone", o.deadZone)), 0.0f, 0.33f);
    o.sensitivity =
        std::clamp(static_cast<float>(ini.getDouble("Controls", "Sensitivity", o.sensitivity)), 0.5f, 2.0f);
    o.usePovHat = ini.getBool("Controls", "UsePovHat", o.usePovHat);
    o.forceFeedback = ini.getBool("Controls", "ForceFeedback", o.forceFeedback);
    return o;
}

namespace {
float signOf(float v) { return 0.0f < v ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
float powf32(float base, float exponent) {
    return static_cast<float>(std::pow(static_cast<double>(base), static_cast<double>(exponent)));
}
} // namespace

void AnalogSteering::setSpeed(float speed, float sensitivity) {
    if (speedSensitive == 2) {
        float v = speedBaseLow;
        if (speedBaseLow <= speed)
            v = speed < speedBaseHi ? speed : speedBaseHi;
        const float f = v / (speedBaseHi - speedBaseLow);
        auto lerp = [f](float lo, float hi) { return (hi - lo) * f + lo; };
        m_mouseSensitivity = lerp(mouseSensitivityLow, mouseSensitivityHi) * sensitivity;
        m_mouseExponent = lerp(mouseFilterLow, mouseFilterHi);
        m_joySens = sensitivity * lerp(joystick.sensitivityLow, joystick.sensitivityHi);
        m_joyExp = lerp(joystick.filterLow, joystick.filterHi);
        m_joyOut = lerp(joystick.approachOutLow, joystick.approachOutHi);
        m_joyIn = lerp(joystick.approachInLow, joystick.approachInHi);
        m_wheelSens = sensitivity * lerp(wheel.sensitivityLow, wheel.sensitivityHi);
        m_wheelExp = lerp(wheel.filterLow, wheel.filterHi);
        m_wheelOut = lerp(wheel.approachOutLow, wheel.approachOutHi);
        m_wheelIn = lerp(wheel.approachInLow, wheel.approachInHi);
        return;
    }
    // mmPlayer::Update without speed sensitivity: the Low values (0) or the
    // Hi values. (With 0 the original also multiplies the stored joystick
    // and wheel Low sensitivities by the option every frame; not kept.)
    const bool low = speedSensitive == 0;
    m_mouseSensitivity = (low ? mouseSensitivityLow : mouseSensitivityHi) * sensitivity;
    m_mouseExponent = low ? mouseFilterLow : mouseFilterHi;
    m_joySens = sensitivity * (low ? joystick.sensitivityLow : joystick.sensitivityHi);
    m_joyExp = low ? joystick.filterLow : joystick.filterHi;
    m_joyOut = low ? joystick.approachOutLow : joystick.approachOutHi;
    m_joyIn = low ? joystick.approachInLow : joystick.approachInHi;
    m_wheelSens = sensitivity * (low ? wheel.sensitivityLow : wheel.sensitivityHi);
    m_wheelExp = low ? wheel.filterLow : wheel.filterHi;
    m_wheelOut = low ? wheel.approachOutLow : wheel.approachOutHi;
    m_wheelIn = low ? wheel.approachInLow : wheel.approachInHi;
}

float AnalogSteering::mouseAxis(float x, float width) const {
    if (!(width > 0.0f))
        return 0.0f;
    const float u = x * (1.0f / width);
    return (u + u - 1.0f) / m_mouseSensitivity;
}

float AnalogSteering::deviceFilter(const DeviceParams& p, float sens, float exponent, float out, float in, float axis,
                                   float dt) {
    // Held past the threshold: the time counts up, else it is 0.
    const float a = std::abs(axis);
    if (a > threshold)
        m_held += dt;
    else
        m_held = 0.0f;
    // Above a sensitivity of 1 a stick past the threshold is full lock;
    // otherwise the axis over the sensitivity, within -1 .. 1.
    float v;
    if (sens <= 1.0f || a <= threshold) {
        v = axis / sens;
        if (v < -1.0f)
            v = -1.0f;
        else if (v > 1.0f)
            v = 1.0f;
    } else {
        v = signOf(axis);
    }
    if (!p.approach || m_held == 0.0f) {
        m_position = v;
        return powf32(std::abs(v) * sens, exponent) * (1.0f / sens) * signOf(v);
    }
    // The approach: further out the same way at Out, back or across at In,
    // both growing with the time held.
    const bool outward = !(std::abs(v) < std::abs(m_position)) && signOf(m_position) == signOf(v);
    const float rate = p.approachGrowth * m_held + (outward ? out : in);
    if (m_position < v) {
        m_position = rate * dt + m_position;
        if (m_position > v)
            m_position = v;
    } else if (m_position > v) {
        m_position = m_position - rate * dt;
        if (m_position < v)
            m_position = v;
    }
    return powf32(std::abs(m_position), exponent) * signOf(m_position);
}

float AnalogSteering::filter(Controller controller, float axis, float dt) {
    switch (controller) {
    case Controller::Mouse: return powf32(std::abs(axis), m_mouseExponent) * signOf(axis);
    case Controller::Joystick: return deviceFilter(joystick, m_joySens, m_joyExp, m_joyOut, m_joyIn, axis, dt);
    case Controller::Wheel: return deviceFilter(wheel, m_wheelSens, m_wheelExp, m_wheelOut, m_wheelIn, axis, dt);
    default: return axis;
    }
}

float applyDeadZone(float value, float deadZone) {
    const float a = std::abs(value);
    if (a <= deadZone || deadZone >= 1.0f)
        return 0.0f;
    return sign(value) * std::min(1.0f, (a - deadZone) / (1.0f - deadZone));
}

} // namespace mm2::app::controls
