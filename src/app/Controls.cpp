#include "app/Controls.h"

#include "core/Libm.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace mm2::app::controls {
namespace {

using platform::Key;

// mmInput::AttachToPipe (IOInit: string, slot, type) and mmInput::
// SetDefaultConfig, keyboard device (1): DirectInput key codes in slot order
// (TAB, Q, E, F, H, -, LEFT, RIGHT, UP, DOWN, SPACE, C, V, RETURN,
// NUMPAD4/6/2/8, W, D, T, A, Z, R, S, X, 2, 3, 4, 5, BACKSPACE, -, I, Y).
constexpr std::array<ActionInfo, kActionCount> kActions = {{
    {296, Key::Tab, true, SlotKind::Event},              // Map Toggle
    {298, Key::Q, true, SlotKind::Event},                // Full Screen Map
    {299, Key::E, true, SlotKind::Event},                // Map Zoom
    {300, Key::F, true, SlotKind::Event},                // Rotating Map
    {297, Key::H, true, SlotKind::Event},                // HUD Toggle
    {282, Key::Unknown, false, SlotKind::Axis},          // Steering
    {283, Key::Left, true, SlotKind::Button},            // Steer Left
    {284, Key::Right, true, SlotKind::Button},           // Steer Right
    {280, Key::Up, true, SlotKind::ButtonOrAxis},        // Throttle
    {281, Key::Down, true, SlotKind::ButtonOrAxis},      // Brakes
    {307, Key::Space, true, SlotKind::Button},           // Handbrake
    {276, Key::C, true, SlotKind::Event},                // Change Camera
    {277, Key::V, true, SlotKind::Event},                // Thrill Cam
    {279, Key::Return, true, SlotKind::ButtonOrAxis},    // Horn
    {286, Key::Kp4, true, SlotKind::ButtonOrAxis},       // Look Left
    {285, Key::Kp6, true, SlotKind::ButtonOrAxis},       // Look Right
    {287, Key::Kp2, true, SlotKind::ButtonOrAxis},       // Look Back
    {288, Key::Kp8, true, SlotKind::ButtonOrAxis},       // Look Forward
    {289, Key::W, true, SlotKind::Event},                // Wide Angle
    {290, Key::D, true, SlotKind::Event},                // Dashboard On/Off
    {278, Key::T, true, SlotKind::Event},                // Transmission
    {291, Key::A, true, SlotKind::Event},                // Shift Up
    {292, Key::Z, true, SlotKind::Event},                // Shift Down
    {293, Key::R, true, SlotKind::Event},                // Reverse
    {294, Key::S, true, SlotKind::Event},                // Next Checkpoint
    {295, Key::X, true, SlotKind::Event},                // Prev. Checkpoint
    {301, Key::Num2, true, SlotKind::Event},             // Toggle CD Player
    {302, Key::Num3, true, SlotKind::Event},             // Start/Stop CD
    {304, Key::Num4, true, SlotKind::Event},             // Prev. CD Track
    {303, Key::Num5, true, SlotKind::Event},             // Next CD Track
    {305, Key::Backspace, true, SlotKind::Event},        // Rear View Mirror
    {306, Key::Unknown, false, SlotKind::Axis},          // Camera Pan
    {308, Key::I, true, SlotKind::Event},                // Opponent Position
    {309, Key::Y, true, SlotKind::Event},                // Enter Chat Msg
}};

float sign(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }

constexpr std::size_t idx(Action a) { return static_cast<std::size_t>(a); }

// The controllers' names in their [Controls] keys.
constexpr std::array<const char*, kControllerCount> kControllerNames = {"Mouse", "Keyboard", "Joystick", "GamePad",
                                                                         "Wheel"};

// The mouse's and the joystick's components by their stored names.
struct NamedComponent {
    const char* name;
    int component;
};
constexpr std::array<NamedComponent, 4> kMouseNames = {{
    {"Left", component::kMouseLeft},
    {"Right", component::kMouseRight},
    {"X", component::kMouseX},
    {"Y", component::kMouseY},
}};
constexpr std::array<NamedComponent, 11> kJoyAxisNames = {{
    {"X", component::kJoyX},
    {"Y", component::kJoyY},
    {"Z", component::kJoyZ},
    {"U", component::kJoyU},
    {"R", component::kJoyR},
    {"V", component::kJoyV},
    {"POV", component::kJoyPov},
    {"X Left", component::kJoyXLeft},
    {"X Right", component::kJoyXRight},
    {"Y Up", component::kJoyYUp},
    {"Y Down", component::kJoyYDown},
}};

bool isJoyButton(int c) { return component::kJoyButton1 <= c && c <= component::kJoyButton12; }

// mmIO::CompareComponent: a slot bound to `have` uses the control `b` when
// the device and component match, or when `have` is a whole joystick axis
// and `b` one of its halves (not the other way round). MM2 compares the
// numbers whatever the device, so DirectInput's keys 10 / 11 ('9', '0')
// also clash with 0x11-0x14 (W, E, R, T); OpenMM2's keys are SDL scancodes
// and only the joystick's axes are compared that way.
bool sameControl(Binding have, Binding b) {
    if (have.device != b.device || have.device == Device::None)
        return false;
    if (have.component == b.component)
        return true;
    if (b.device != Device::Joystick)
        return false;
    return (have.component == component::kJoyX &&
            (b.component == component::kJoyXLeft || b.component == component::kJoyXRight)) ||
           (have.component == component::kJoyY &&
            (b.component == component::kJoyYUp || b.component == component::kJoyYDown));
}

} // namespace

const std::array<ActionInfo, kActionCount>& actions() { return kActions; }

const ActionInfo& info(Action a) { return kActions[idx(a)]; }

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

// --- Binding sets ------------------------------------------------------------------------------

bool isAxisComponent(Binding b) {
    // mmIODev::GetComponentType.
    switch (b.device) {
    case Device::Mouse: return b.component == component::kMouseX || b.component == component::kMouseY;
    case Device::Joystick: return 9 < b.component && b.component < 0x15;
    default: return false;
    }
}

IoType assignedIoType(SlotKind kind, Binding b) {
    // mmIODev::Assign: a press slot (type bit 1) takes the mouse's buttons,
    // any key and the joystick's buttons; the other slots also take the
    // mouse's axes, and any other joystick component as an axis.
    const int c = b.component;
    if ((static_cast<int>(kind) & 1) != 0) {
        switch (b.device) {
        case Device::Mouse:
            return c == component::kMouseLeft || c == component::kMouseRight ? IoType::Event : IoType::None;
        case Device::Keyboard: return IoType::Event;
        case Device::Joystick: return isJoyButton(c) ? IoType::Event : IoType::None;
        default: return IoType::None;
        }
    }
    switch (b.device) {
    case Device::Mouse:
        if (c == component::kMouseLeft || c == component::kMouseRight)
            return IoType::Held;
        return c == component::kMouseX || c == component::kMouseY ? IoType::Axis : IoType::None;
    case Device::Keyboard: return IoType::Held;
    case Device::Joystick: return isJoyButton(c) ? IoType::Held : IoType::Axis;
    default: return IoType::None;
    }
}

BindingSet defaultBindings(Controller c, int joystickButtons) {
    // mmInput::SetDefaultConfig. Every set starts from the keyboard's keys;
    // the controllers differ in the driving slots and the buttons below.
    using component::joyButton;
    BindingSet set{};
    for (std::size_t i = 0; i < kActionCount; ++i)
        set[i] = kActions[i].key == Key::Unknown ? Binding{} : Binding::key(kActions[i].key);
    // Camera Pan reads the joystick's POV hat in every set (the keyboard
    // and the game pad do not read the slot; see slotEnabled).
    set[idx(Action::CameraPan)] = Binding::joy(component::kJoyPov);
    auto at = [&set](Action a) -> Binding& { return set[idx(a)]; };
    switch (c) {
    case Controller::Mouse:
        // The cursor's X steers, the left and right buttons drive.
        at(Action::Steering) = Binding::mouse(component::kMouseX);
        at(Action::Throttle) = Binding::mouse(component::kMouseLeft);
        at(Action::Brakes) = Binding::mouse(component::kMouseRight);
        break;
    case Controller::Keyboard:
        // The steering slot gets the "8" key (DirectInput 9); the keyboard
        // never reads the slot.
        at(Action::Steering) = Binding::key(Key::Num8);
        break;
    case Controller::Joystick:
        at(Action::MapToggle) = Binding::joy(joyButton(4));
        at(Action::Steering) = Binding::joy(component::kJoyX);
        at(Action::Throttle) = Binding::joy(component::kJoyYUp);
        at(Action::Brakes) = Binding::joy(component::kJoyYDown);
        at(Action::Handbrake) = Binding::joy(joyButton(1));
        at(Action::ChangeCamera) = Binding::joy(joyButton(3));
        at(Action::Horn) = Binding::joy(joyButton(2));
        break;
    case Controller::GamePad:
        at(Action::Steering) = Binding::joy(component::kJoyX);
        at(Action::Throttle) = Binding::joy(joyButton(1));
        at(Action::Brakes) = Binding::joy(joyButton(2));
        at(Action::Handbrake) = Binding::joy(joyButton(4));
        at(Action::Horn) = Binding::joy(joyButton(3));
        at(Action::WideAngle) = Binding::joy(joyButton(6));
        at(Action::Dashboard) = Binding::joy(joyButton(5));
        at(Action::ShiftUp) = Binding::joy(joyButton(8));
        at(Action::ShiftDown) = Binding::joy(joyButton(7));
        break;
    case Controller::Wheel:
        at(Action::Steering) = Binding::joy(component::kJoyX);
        at(Action::Throttle) = Binding::joy(component::kJoyYUp);
        at(Action::Brakes) = Binding::joy(component::kJoyYDown);
        at(Action::Handbrake) = Binding::joy(joyButton(1));
        at(Action::Horn) = Binding::joy(joyButton(2));
        if (joystickButtons >= 6) {
            at(Action::ChangeCamera) = Binding::joy(joyButton(3));
            at(Action::HudToggle) = Binding::joy(joyButton(4));
            at(Action::MapToggle) = Binding::joy(joyButton(5));
            at(Action::RearViewMirror) = Binding::joy(joyButton(6));
        }
        if (joystickButtons >= 8) {
            at(Action::ShiftUp) = Binding::joy(joyButton(7));
            at(Action::ShiftDown) = Binding::joy(joyButton(8));
        }
        break;
    }
    return set;
}

bool slotEnabled(Controller c, Action a) {
    // mmInput::Init: the keyboard does not read the steering axis or the
    // camera pan; the others do not read Steer Left / Right, and only the
    // joystick reads the camera pan.
    switch (c) {
    case Controller::Keyboard: return a != Action::Steering && a != Action::CameraPan;
    case Controller::Joystick: return a != Action::SteerLeft && a != Action::SteerRight;
    default: return a != Action::SteerLeft && a != Action::SteerRight && a != Action::CameraPan;
    }
}

bool slotLocked(Controller c, Action a) {
    // mmInput::Init: the steering axis is fixed except for the game pad (and
    // the keyboard, which does not read it).
    return a == Action::Steering && c != Controller::Keyboard && c != Controller::GamePad;
}

std::string bindingKey(Controller c, std::uint32_t stringId) {
    if (c == Controller::Keyboard)
        return bindKey(stringId);
    return std::format("Bind.{}.{}", kControllerNames[static_cast<std::size_t>(c)], stringId);
}

std::string bindingText(Binding b) {
    switch (b.device) {
    case Device::Keyboard: {
        std::string name = platform::keyName(static_cast<Key>(b.component));
        return name.empty() ? std::string(kUnbound) : name;
    }
    case Device::Mouse:
        for (const auto& n : kMouseNames)
            if (n.component == b.component)
                return std::format("Mouse {}", n.name);
        break;
    case Device::Joystick:
        if (isJoyButton(b.component) || b.component == component::kJoyButton12 + 1)
            return std::format("Joy Button {}", b.component - component::kJoyButton1 + 1);
        for (const auto& n : kJoyAxisNames)
            if (n.component == b.component)
                return std::format("Joy {}", n.name);
        break;
    default: break;
    }
    return kUnbound;
}

std::optional<Binding> parseBinding(std::string_view text) {
    if (text.empty())
        return std::nullopt;
    if (text == kUnbound)
        return Binding{};
    if (text.starts_with("Mouse ")) {
        const std::string_view rest = text.substr(6);
        for (const auto& n : kMouseNames)
            if (rest == n.name)
                return Binding::mouse(n.component);
        return std::nullopt;
    }
    if (text.starts_with("Joy ")) {
        const std::string_view rest = text.substr(4);
        if (rest.starts_with("Button ")) {
            const std::string_view num = rest.substr(7);
            int n = 0;
            const auto r = std::from_chars(num.data(), num.data() + num.size(), n);
            if (r.ec != std::errc{} || r.ptr != num.data() + num.size() || n < 1 || n > 13)
                return std::nullopt;
            return Binding::joy(component::kJoyButton1 + n - 1);
        }
        for (const auto& n : kJoyAxisNames)
            if (rest == n.name)
                return Binding::joy(n.component);
        return std::nullopt;
    }
    const Key k = platform::keyFromName(text);
    if (k == Key::Unknown)
        return std::nullopt;
    return Binding::key(k);
}

BindingSet loadBindings(const IniFile& ini, Controller c, int joystickButtons) {
    BindingSet set = defaultBindings(c, joystickButtons);
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const std::string text = ini.getString("Controls", bindingKey(c, kActions[i].stringId));
        if (const auto b = parseBinding(text))
            set[i] = *b;
    }
    return set;
}

void storeBinding(IniFile& ini, Controller c, Action a, Binding b) {
    ini.set("Controls", bindingKey(c, info(a).stringId), bindingText(b));
}

void clearBindings(IniFile& ini, Controller c) {
    for (const auto& a : kActions)
        ini.remove("Controls", bindingKey(c, a.stringId));
}

std::string describe(Binding b, const std::function<std::string(std::uint32_t, const char*)>& string) {
    // mmIODev::GetDescription.
    const std::string undefined = string(271, "UNDEFINED");
    switch (b.device) {
    case Device::Mouse:
        switch (b.component) {
        case component::kMouseLeft: return string(310, "Left Mouse Button");
        case component::kMouseRight: return string(311, "Right Mouse Button");
        case component::kMouseX: return string(312, "Mouse X-Axis");
        case component::kMouseY: return string(313, "Mouse Y-Axis");
        default: return undefined;
        }
    case Device::Keyboard: {
        // ConvertDItoString's DirectInput key name; OpenMM2 names the key
        // the platform layer's way.
        std::string name = platform::keyName(static_cast<Key>(b.component));
        return name.empty() ? undefined : name;
    }
    case Device::Joystick: {
        const std::string joy = string(273, "Joy");
        if (isJoyButton(b.component))
            return std::format("{} {} {}", joy, string(272, "Button"), b.component - 0x14);
        struct Axis {
            int component;
            std::uint32_t id;
            const char* fallback;
        };
        static constexpr std::array<Axis, 11> kAxes = {{
            {component::kJoyX, 314, "X-Axis"},
            {component::kJoyXLeft, 315, "X-Axis Left"},
            {component::kJoyXRight, 316, "X-Axis Right"},
            {component::kJoyY, 317, "Y-Axis"},
            {component::kJoyYUp, 318, "Y-Axis Up"},
            {component::kJoyYDown, 319, "Y-Axis Down"},
            {component::kJoyZ, 320, "Z-Axis"},
            {component::kJoyU, 321, "U-Axis"},
            {component::kJoyR, 322, "R-Axis"},
            {component::kJoyV, 323, "V-Axis"},
            {component::kJoyPov, 324, "POV-Axis"},
        }};
        for (const auto& a : kAxes)
            if (a.component == b.component)
                return std::format("{} {}", joy, string(a.id, a.fallback));
        // Anything else (a 13th button) shows UNDEFINED.
        return undefined;
    }
    default: return undefined;
    }
}

// --- Rebinding ---------------------------------------------------------------------------------

int axisCaptured(float rest, float value) {
    // mmJaxis::ResetCapture keeps the resting value and the thresholds
    // -0.125 / 0.125; mmJaxis::Capture compares the travel with them.
    const float travel = value - rest;
    if (travel < -0.125f)
        return -1;
    if (0.125f < travel)
        return 1;
    return 0;
}

bool Rebinder::sanityCheck(Action slot, Binding b) const {
    // mmInput::SanityCheck / mmIODev::SanityCheckioType: the control's kind
    // (axis or button) must match how the slot is read now; Throttle,
    // Brakes and Handbrake may change between the two. A press slot takes
    // only buttons. The slot's reading is its binding's (an unbound slot's
    // is its default binding's; MM2 keeps the old reading, inferred).
    const std::size_t i = idx(slot);
    Binding current = m_set[i];
    if (current.device == Device::None)
        current = defaultBindings(m_controller, 12)[i];
    const IoType io = assignedIoType(info(slot).kind, current);
    const IoType type = isAxisComponent(b) ? IoType::Axis : IoType::Held;
    const bool canChange = slot == Action::Throttle || slot == Action::Brakes || slot == Action::Handbrake;
    switch (io) {
    case IoType::Held:
    case IoType::Axis: return type == io || canChange;
    case IoType::Event: return type != IoType::Axis;
    default: return true; // SanityCheckioType's 0 passes too
    }
}

int Rebinder::alreadyAssigned(Action slot, Binding b, Action* other) const {
    // mmInput::IsAlreadyAssigned: another slot of the set using the same
    // control (mmIO::CompareComponent: the same component, or a full X or Y
    // axis when the new control is one of its halves). A fixed slot refuses
    // (-1); a listed one is a duplicate, reported as its index in MM2's
    // device table (so the mouse set's Map Toggle, index 0, is missed).
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action a = static_cast<Action>(i);
        if (a == slot || !sameControl(m_set[i], b))
            continue;
        if (slotLocked(m_controller, a))
            return -1;
        if (slotEnabled(m_controller, a)) {
            if (other)
                *other = a;
            return static_cast<int>(i) + static_cast<int>(m_controller) * static_cast<int>(kActionCount);
        }
    }
    return 0;
}

void Rebinder::assign(Action slot, Binding b) {
    // mmInput::AssignIO: mmIODev::Assign (which leaves the slot alone when
    // it cannot take the control), then every other slot using the control
    // is unbound (IsAlreadyAssigned's forced pass).
    const std::size_t i = idx(slot);
    if (assignedIoType(info(slot).kind, b) != IoType::None)
        m_set[i] = b;
    for (std::size_t j = 0; j < kActionCount; ++j)
        if (j != i && sameControl(m_set[j], b))
            m_set[j] = Binding{};
}

CaptureResult Rebinder::capture(Action slot, const Captured& what, Action* other) {
    // mmInput::BuildCaptureIO: the captured state as a device and a
    // component.
    Binding b;
    switch (what.kind) {
    case Captured::Kind::Key: b = {Device::Keyboard, what.value}; break;
    case Captured::Kind::Mouse:
        if (what.value != component::kMouseLeft && what.value != component::kMouseRight)
            return CaptureResult::Rejected;
        b = Binding::mouse(what.value);
        break;
    case Captured::Kind::JoyButton:
        // mmJoyMan::GetOneButton numbers the first sixteen from 1; buttons
        // past 13 are refused.
        if (what.value < 1 || 13 < what.value)
            return CaptureResult::Rejected;
        b = Binding::joy(what.value + 0x14);
        break;
    case Captured::Kind::JoyAxis: {
        int c = what.value;
        // The steering slot takes the whole axis for a half.
        if (slot == Action::Steering) {
            if (c == component::kJoyXLeft || c == component::kJoyXRight)
                c = component::kJoyX;
            else if (c == component::kJoyYUp || c == component::kJoyYDown)
                c = component::kJoyY;
        }
        b = Binding::joy(c);
        break;
    }
    default: return CaptureResult::Rejected;
    }
    m_last = b;
    if (!sanityCheck(slot, b))
        return CaptureResult::Rejected;
    const int dup = alreadyAssigned(slot, b, other);
    if (dup == 0) {
        assign(slot, b);
        return CaptureResult::Assigned;
    }
    return dup > 0 ? CaptureResult::Duplicate : CaptureResult::Rejected;
}

void Rebinder::forceAssign(Action slot) { assign(slot, m_last); }

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
    // mmInput::SetForceFeedbackScale / SetRoadForceScale's 0 .. 2.
    o.ffCollision = std::clamp(static_cast<float>(ini.getDouble("Controls", "FFCollision", o.ffCollision)), 0.0f, 2.0f);
    o.ffRoadForce = std::clamp(static_cast<float>(ini.getDouble("Controls", "FFRoadForce", o.ffRoadForce)), 0.0f, 2.0f);
    return o;
}

namespace {
float signOf(float v) { return 0.0f < v ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
float powf32(float base, float exponent) {
    return static_cast<float>(libm::pow(static_cast<double>(base), static_cast<double>(exponent)));
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
