#pragma once

// The player's in-race controls as Midtown Madness 2's mmInput reads them:
// the 34 action slots (mmInput::AttachToPipe / IOInit), the five
// controllers' binding sets with their defaults (mmInput::SetDefaultConfig)
// and which slots each controller reads (mmInput::Init), the [Controls]
// bindings the options menu stores, rebinding a slot to a key, a mouse
// button or a joystick control (mmInput::BuildCaptureIO and its checks), the
// analog steering filter and the joystick dead zone. GameInput.h turns a
// binding set into the game's inputs each frame.

#include "core/Ini.h"
#include "platform/Input.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mm2::app::controls {

// mmInput's action slots, in the order of its event numbers (the order the
// customize-controls list shows them).
enum class Action : std::uint8_t {
    MapToggle,        // mmViewMgr::SetViewSetting(1)
    FullScreenMap,    // (10); pauses a single-player game
    MapZoom,          // (7) mmHudMap::ToggleMapRes
    RotatingMap,      // (8) mmHudMap::ToggleMapOrient
    HudToggle,        // (4) mmHUD::ToggleExternalView: the instrument cluster
    Steering,         // an axis
    SteerLeft,
    SteerRight,
    Throttle,
    Brakes,
    Handbrake,
    ChangeCamera,     // (0)
    ThrillCam,        // (2)
    Horn,
    LookLeft,
    LookRight,
    LookBack,
    LookForward,
    WideAngle,        // (5)
    Dashboard,        // (6)
    Transmission,     // automatic / manual
    ShiftUp,
    ShiftDown,
    Reverse,
    NextCheckpoint,   // the mode's UpdateGameInput (mmSingleRace)
    PrevCheckpoint,
    ToggleCdPlayer,
    StartStopCd,
    PrevCdTrack,
    NextCdTrack,
    RearViewMirror,   // (9)
    CameraPan,        // the joystick's POV hat
    OpponentPosition, // mmGame::SetIconsState
    EnterChat,
    Count
};
inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);

// What a slot is read as (mmIO +8, the type mmInput::AttachToPipe gives
// IOInit): a press that fires the action once (1), a held button (2), an
// axis (4), or either a held button or an axis (6).
enum class SlotKind : std::uint8_t { Event = 1, Button = 2, Axis = 4, ButtonOrAxis = 6 };

struct ActionInfo {
    std::uint32_t stringId; // the action's name in the string table
    platform::Key key;      // mmInput::SetDefaultConfig, keyboard device
    bool keyboard;          // listed for the keyboard (Steering and Camera Pan are axes)
    SlotKind kind;          // mmInput::AttachToPipe
};

const std::array<ActionInfo, kActionCount>& actions();
const ActionInfo& info(Action a);

// MM2's five controller types ([Controls] Controller; mmInput's device
// global, the options page's CONTROLLERS list, strings 580-584). Each has
// its own binding set (mmInput::SetDefaultConfig).
enum class Controller : std::uint8_t { Mouse, Keyboard, Joystick, GamePad, Wheel };
inline constexpr int kControllerCount = 5;

// [Controls] Bind.<string id> = <key name>; "Undefined" leaves it unbound.
// This is the keyboard controller's set; bindingKey() names the others.
std::string bindKey(std::uint32_t stringId);
inline constexpr const char* kUnbound = "Undefined";

// The key bound to one action: the stored binding, else the default.
platform::Key boundKey(const IniFile& ini, const ActionInfo& a);

class Bindings {
public:
    // Reads the [Controls] bindings over the keyboard defaults.
    void load(const IniFile& ini);
    platform::Key key(Action a) const { return m_keys[static_cast<std::size_t>(a)]; }
    bool down(const platform::Input& in, Action a) const;
    bool pressed(const platform::Input& in, Action a) const;

private:
    std::array<platform::Key, kActionCount> m_keys{};
};

// --- Binding sets (mmIO / mmIODev) -------------------------------------------------------

// mmIODev's device codes (+0x98): the mouse, the keyboard and the first
// joystick (MM2 numbers further joysticks 5-7, but mmJoyMan::Init only ever
// opens one).
enum class Device : std::uint8_t { None = 0, Mouse = 2, Keyboard = 3, Joystick = 4 };

// mmIODev's components (+0x9c). The keyboard's is the key itself.
namespace component {
inline constexpr int kMouseLeft = 1, kMouseRight = 2; // the button bits
inline constexpr int kMouseX = 10, kMouseY = 11;
// mmJoystick::GetAxis: X, Y, Z (read negated), U and V (never polled, so 0),
// R (the Rz axis), the POV hat, and the two halves of X and Y.
inline constexpr int kJoyX = 10, kJoyY = 11, kJoyZ = 12, kJoyU = 13, kJoyR = 14, kJoyV = 15, kJoyPov = 16;
inline constexpr int kJoyXLeft = 0x11, kJoyXRight = 0x12, kJoyYUp = 0x13, kJoyYDown = 0x14;
// Buttons 1-12 (mmJoyMan::ButtonToBit: bit n - 1).
inline constexpr int kJoyButton1 = 0x15, kJoyButton12 = 0x20;
inline constexpr int joyButton(int n) { return kJoyButton1 + n - 1; }
} // namespace component

struct Binding {
    Device device = Device::None;
    int component = 0;
    bool operator==(const Binding&) const = default;
    static Binding key(platform::Key k) { return {Device::Keyboard, static_cast<int>(k)}; }
    static Binding mouse(int c) { return {Device::Mouse, c}; }
    static Binding joy(int c) { return {Device::Joystick, c}; }
};
using BindingSet = std::array<Binding, kActionCount>;

// How a bound slot is read (mmIODev +0x88): not at all, as a held button
// (1), as an axis (2), or as presses (3).
enum class IoType : std::uint8_t { None = 0, Held = 1, Axis = 2, Event = 3 };

// mmIODev::GetComponentType: an axis (the mouse's X and Y, the joystick's
// 10-0x14) or a button (everything else).
bool isAxisComponent(Binding b);
// mmIODev::Assign: the reading a control gets in a slot of `kind`, or None
// when the slot cannot take it (a press slot takes only buttons and keys).
IoType assignedIoType(SlotKind kind, Binding b);

// mmInput::SetDefaultConfig for one controller. The wheel's set depends on
// its button count (`joystickButtons`, below 0 without a joystick): with six
// buttons or more Change Camera, HUD Toggle, Map Toggle and Rear View
// Mirror move to buttons 3-6, with eight Shift Up and Down to buttons 7-8.
BindingSet defaultBindings(Controller c, int joystickButtons);

// mmInput::Init: whether a controller reads a slot (mmIO +0x14), and whether
// the slot is fixed (+0x18, the steering axis of the joystick, wheel and
// mouse). The customize list shows the slots that are read and not fixed.
bool slotEnabled(Controller c, Action a);
bool slotLocked(Controller c, Action a);
inline bool slotListed(Controller c, Action a) { return slotEnabled(c, a) && !slotLocked(c, a); }

// [Controls] keys of the five sets: the keyboard's are Bind.<string id> (as
// before), the others Bind.<Mouse|Joystick|GamePad|Wheel>.<string id>.
std::string bindingKey(Controller c, std::uint32_t stringId);
// The stored text of a binding: a key name, "Mouse Left" / "Mouse Right" /
// "Mouse X" / "Mouse Y", "Joy X" ... "Joy Button 12", or "Undefined".
std::string bindingText(Binding b);
std::optional<Binding> parseBinding(std::string_view text);
// A controller's set: each stored binding over the default.
BindingSet loadBindings(const IniFile& ini, Controller c, int joystickButtons);
void storeBinding(IniFile& ini, Controller c, Action a, Binding b);
// Forgets a controller's stored bindings (its defaults return).
void clearBindings(IniFile& ini, Controller c);

// mmIODev::GetDescription: what the customize list shows ("Left Mouse
// Button", the key's name, "Joy Button 3", "Joy X-Axis Left", or
// UNDEFINED), with the game's strings 271-324.
std::string describe(Binding b, const std::function<std::string(std::uint32_t, const char*)>& string);

// --- Rebinding (mmInput::BuildCaptureIO) -----------------------------------------------------

// What the capture saw this frame (mmInput::PollStates): one newly pressed
// key, a mouse button, a joystick button (1-based) or a joystick axis that
// moved 0.125 from where it rested (mmJaxis::Capture: the component).
struct Captured {
    enum class Kind : std::uint8_t { None, Key, Mouse, JoyButton, JoyAxis } kind = Kind::None;
    int value = 0;
};

enum class CaptureResult : std::uint8_t {
    Rejected,  // the control cannot serve the slot (BuildCaptureIO returns 0)
    Assigned,  // bound (1)
    Duplicate, // another listed slot uses it (2): bind anyway with forceAssign
};

class Rebinder {
public:
    Rebinder(BindingSet& set, Controller c) : m_set(set), m_controller(c) {}
    // mmInput::BuildCaptureIO for `slot`; `other` receives the slot that
    // already uses the control.
    CaptureResult capture(Action slot, const Captured& what, Action* other = nullptr);
    // mmInput::ForceAssignment: the last capture's control goes to `slot`
    // and every other slot using it is unbound.
    void forceAssign(Action slot);
    Binding last() const { return m_last; }

private:
    bool sanityCheck(Action slot, Binding b) const;
    int alreadyAssigned(Action slot, Binding b, Action* other) const;
    void assign(Action slot, Binding b);
    BindingSet& m_set;
    Controller m_controller;
    Binding m_last; // mmInput +0x1f4 / +0x1f8
};

// mmJaxis::Capture: an axis counts as moved once it is more than 0.125 away
// from where it rested when the capture began: -1, 0 or 1.
int axisCaptured(float rest, float value);

// The [Controls] options (mmPlayerConfig::DefaultControls defaults).
struct Options {
    Controller controller = Controller::Keyboard;
    bool autoReverse = true; // AUTO REVERSE (mmInput +0x18C)
    float deadZone = 0.1f;   // CONTROLLER DEAD ZONE, 0 .. 0.33 (mmInput +0x1B4)
    float sensitivity = 1.0f; // STEERING SENSITIVITY, 0.5 .. 2 (mouse and joysticks)
    bool usePovHat = false;   // POV HAT (mmInput +0x184): stored only, as in MM2
    bool forceFeedback = false;
    float ffCollision = 1.0f; // COLLISION intensity, 0 .. 2 (mmInput +0x1AC)
    float ffRoadForce = 1.0f; // ROAD FORCE intensity, 0 .. 2 (mmInput +0x1B0)
    static Options load(const IniFile& ini);
};

// mmPlayer::FilterSteering, the analog devices' steering (mmInput::
// GetSteering passes it the axis): the mouse's curve, and for the joystick
// and the wheel a sensitivity, a curve and (with JoyApp / WheelApp, off by
// default) a rate-limited approach. Its parameters are blended by speed in
// mmPlayer::Update with f = clamp(speed, SpeedBaseLow, SpeedBaseHi) /
// (SpeedBaseHi - SpeedBaseLow); the defaults are mmPlayer's constructor's
// (the names are mmPlayer::FileIO's), which tune/<car>.asnode replaces
// (GameInput::setPlayerTune).
class AnalogSteering {
public:
    struct DeviceParams {
        float sensitivityLow = 0.5f, sensitivityHi = 1.1f; // Joy/WheelSensitivityLow/Hi
        float filterLow = 1.0f, filterHi = 3.0f;           // Joy/WheelSteerFilterLow/Hi (the curve's exponent)
        bool approach = false;                             // JoyApp / WheelApp
        float approachOutLow = 10.0f, approachOutHi = 2.0f; // further out the same way
        float approachInLow = 10.0f, approachInHi = 4.0f;   // back in or across (JoySteerApproachIn...)
        float approachGrowth = 0.0f;                         // JoySteerAppApp: added per second held
    };
    int speedSensitive = 2; // 0 the Low values, 2 blended by speed, else the Hi values
    float speedBaseLow = 5.0f, speedBaseHi = 100.0f;
    float mouseSensitivityLow = 0.6f, mouseSensitivityHi = 1.8f;
    float mouseFilterLow = 1.5f, mouseFilterHi = 4.0f;
    DeviceParams joystick, wheel;
    float threshold = 0.99f; // mmPlayer +0x226c: the stick counts as held past it

    // mmPlayer::Update's part: the frame's parameters for the car's forward
    // speed (m/s) and the STEERING SENSITIVITY option (mmInput +0x1c4).
    void setSpeed(float speed, float sensitivity);
    // mmPlayer::FilterSteering for `controller` (others pass through).
    float filter(Controller controller, float axis, float dt);
    // mmInput::PollContinuous, mouse: the cursor's place across the window,
    // -1 .. 1, divided by the mouse sensitivity.
    float mouseAxis(float x, float width) const;
    void reset() { m_position = 0.0f, m_held = 0.0f; }

private:
    float deviceFilter(const DeviceParams& p, float sens, float exponent, float out, float in, float axis, float dt);
    float m_mouseSensitivity = 1.0f, m_mouseExponent = 1.5f; // mmInput +0x1c0, mmPlayer +0x22cc
    float m_joySens = 1.0f, m_joyExp = 1.0f, m_joyOut = 10.0f, m_joyIn = 10.0f;     // +0x2304, +0x2300, +0x22ec, +0x22e0
    float m_wheelSens = 1.0f, m_wheelExp = 1.0f, m_wheelOut = 10.0f, m_wheelIn = 10.0f; // +0x233c, +0x2338, +0x2324, +0x2318
    float m_held = 0.0f;     // mmPlayer +0x2268: seconds past the threshold
    float m_position = 0.0f; // mmPlayer +0x2270
};

// Keyboard and gamepad steering go through phys::SteeringFilter
// (mmInput::FilterDiscreteSteering / FilterGamepadSteering with the rates
// mmPlayer::Update sets).

// mmJoystick::SetDeadZone: DirectInput's DIPROP_DEADZONE (the option x
// 10000) reports the centre within the dead zone and rescales the rest of
// the axis to the full range.
float applyDeadZone(float value, float deadZone);

} // namespace mm2::app::controls
