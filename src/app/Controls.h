#pragma once

// The player's in-race controls as Midtown Madness 2's mmInput reads them:
// the 34 action slots (mmInput::SetDefaultConfig) with their keyboard
// defaults, the [Controls] bindings the options menu stores, the keyboard
// steering filter and the joystick dead zone.

#include "core/Ini.h"
#include "platform/Input.h"

#include <array>
#include <cstdint>
#include <string>

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

struct ActionInfo {
    std::uint32_t stringId; // the action's name in the string table
    platform::Key key;      // mmInput::SetDefaultConfig, keyboard device
    bool keyboard;          // listed for the keyboard (Steering and Camera Pan are axes)
};

const std::array<ActionInfo, static_cast<std::size_t>(Action::Count)>& actions();
const ActionInfo& info(Action a);

// [Controls] Bind.<string id> = <key name>; "Undefined" leaves it unbound.
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
    std::array<platform::Key, static_cast<std::size_t>(Action::Count)> m_keys{};
};

// MM2's five controller types ([Controls] Controller; mmInput's device
// global, the options page's CONTROLLERS list, strings 580-584). Each has
// its own binding set (mmInput::SetDefaultConfig).
enum class Controller : std::uint8_t { Mouse, Keyboard, Joystick, GamePad, Wheel };

// The [Controls] options (mmPlayerConfig::DefaultControls defaults).
struct Options {
    Controller controller = Controller::Keyboard;
    bool autoReverse = true; // AUTO REVERSE (mmInput +0x18C)
    float deadZone = 0.1f;   // CONTROLLER DEAD ZONE, 0 .. 0.33 (mmInput +0x1B4)
    float sensitivity = 1.0f; // STEERING SENSITIVITY, 0.5 .. 2 (mouse and joysticks)
    bool usePovHat = false;
    bool forceFeedback = false;
    static Options load(const IniFile& ini);
};

// mmPlayer::FilterSteering, the analog devices' steering (mmInput::
// GetSteering passes it the axis): the mouse's curve, and for the joystick
// and the wheel a sensitivity, a curve and (with JoyApp / WheelApp, off by
// default) a rate-limited approach. Its parameters are blended by speed in
// mmPlayer::Update with f = clamp(speed, SpeedBaseLow, SpeedBaseHi) /
// (SpeedBaseHi - SpeedBaseLow); the defaults are mmPlayer's constructor's
// (the names are mmPlayer::FileIO's).
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

// The player's driving inputs as mmGame::UpdateSteeringBrakes reads them
// back from mmReplayManager's frame buffer, which mmReplayManager::Update
// fills every frame before the game runs (live play too, not only replays):
// the steering truncated to a signed byte of 127ths, the throttle, brakes
// and handbrake to bytes of 255ths (GetSteering / GetThrottle / GetBrakes /
// GetHandBrakes).
struct ReplayInputs {
    float steering = 0.0f, throttle = 0.0f, brakes = 0.0f, handbrake = 0.0f;
};
ReplayInputs replayQuantize(float steering, float throttle, float brakes, float handbrake);

} // namespace mm2::app::controls
