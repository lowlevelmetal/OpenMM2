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

// The [Controls] options (mmPlayerConfig::DefaultControls defaults).
struct Options {
    bool autoReverse = true; // AUTO REVERSE (mmInput +0x18C)
    float deadZone = 0.1f;   // CONTROLLER DEAD ZONE, 0 .. 0.33 (mmInput +0x1B4)
    float sensitivity = 1.0f; // STEERING SENSITIVITY, 0.5 .. 2 (mouse and joysticks)
    bool usePovHat = false;
    bool forceFeedback = false;
    static Options load(const IniFile& ini);
};

// mmInput::FilterDiscreteSteering (keyboard steering): the held value moves
// toward the target at 1 per second (mmInput +0x1CC steering in, +0x1C8
// back), and the output is its square with its sign (the exponent at
// +0x1D0 is 2).
struct DiscreteSteering {
    float value = 0.0f; // mmInput +0x19C
    float steerInRate = 1.0f;
    float steerOutRate = 1.0f;
    float exponent = 2.0f;
    float update(float target, float dt);
    void reset() { value = 0.0f; }
};

// mmJoystick::SetDeadZone: DirectInput's DIPROP_DEADZONE (the option x
// 10000) reports the centre within the dead zone and rescales the rest of
// the axis to the full range.
float applyDeadZone(float value, float deadZone);

} // namespace mm2::app::controls
