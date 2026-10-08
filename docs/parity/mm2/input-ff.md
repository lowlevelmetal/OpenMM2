# MM2 -> OpenMM2: input-ff

Audited from MM2Recomp (midtown2.exe build 3393) on 2026-10-08.

Summary: 219 reachable functions in 24 classes and the free functions
`playerFilterSteering`, `ConvertDItoString`, `AngelReadKeyString`, and
mmPlayer's force-feedback methods; ported 129 (of which newly ported 110),
replaced 52, not needed 38, open 0. The 51 functions the coverage tool
finds unreachable are listed at the end.

MM2 reads its controls through `mmInput` (an asNode under ROOT, updated
every frame). It holds 34 action slots (`mmIO`, created by
`mmInput::AttachToPipe` through `IOInit`) and five binding sets of 34
`mmIODev` entries, one per controller (mouse, keyboard, joystick, game pad,
wheel); each entry binds a slot to a key, a mouse button or axis, or a
component of the first joystick (an axis, an axis half, the POV hat or one
of twelve buttons) and says how it is read (held, axis, or press). DirectInput
supplies the devices (`ioKeyboard`, `ioMouse`, and `mmJoyMan` / `mmJoystick`
/ `mmJaxis` for the one joystick MM2 opens); the window procedure feeds
`ioEventQueue`, which `eqEventHandler` hands to `eqEventQ` clients (the
menus and mmInput's mouse events). Force feedback is four DirectInput
effects on that joystick (`mmFrictionFF`, `mmCollideFF`, `mmRoadFF`,
`mmSpringFF`), driven by `mmPlayer::UpdateFF` and `FFImpactCallback` and the
road-wave node `mmCarRoadFF`.

OpenMM2 replaces DirectInput and the Windows event plumbing with its SDL3
platform layer (`platform/Input`, `platform/ForceFeedback`). The game side
is in `app/Controls` (the slot table, the five default sets, which slots
each controller reads, storage, descriptions and rebinding),
`app/GameInput` (mmInput's per-frame work, mmJoystick's view of the
joystick, the capture for rebinding, mmReplayManager's quantisation of the
driving inputs) and `app/ForceFeedback` (mmPlayer's force feedback,
mmCarRoadFF and the effects' parameters). The race (`RaceScreen`) and the
control pages (`frontend/PagesOptions`) call into them.

Fixed or added by this audit:

- Every controller's default bindings (`mmInput::SetDefaultConfig`): the
  game pad, joystick and wheel now get their button bindings for the horn,
  cameras, map, HUD, mirror and gears (the wheel's depend on its button
  count), and the joystick's handbrake is button 1, not button 6.
- The binding sets are per controller and may hold mouse buttons and axes
  and joystick buttons, axes, axis halves and the POV (stored as
  `[Controls] Bind.<controller>.<id>`; the keyboard keeps `Bind.<id>`).
- The race reads them as `mmInput::Update` does: the held slots, the
  analog slots (only the joystick and the wheel keep an analog throttle),
  one-shot actions from key presses, mouse buttons (also re-fired by mouse
  movement while held) and joystick buttons going down, the slots each
  controller reads (`mmInput::Init`), and the fallback to the keyboard when
  a joystick type has no joystick (`mmPlayerConfig::SetControls`).
- The joystick's POV hat pans the camera with the joystick controller
  (`mmInput::GetCamPan`).
- The driving inputs are recorded and replayed as bytes every frame, as
  `mmReplayManager::Update` does (steering x127 signed, pedals x255).
- Rebinding of mouse buttons and joystick controls (`mmInput::
  BuildCaptureIO`, `PollStates`, `mmJaxis::Capture`) on the customize page.
- Force feedback (`mmPlayer::UpdateFF`, `FFImpactCallback`, `ResetFF`,
  `mmCarRoadFF`, the four effects) through SDL haptics, or rumble.
- The steering bars: `mmMouseSteerBar` on the control page, and the
  mouse's bar with the instrument cluster (`mmExternalView::Cull`).
- `mmInput::Flush` on the popups; the POV lamp for a joystick with a hat.

## mmInput

The game's input object: the slots, the per-frame reading, the driving
inputs, the capture for the customize page and the force-feedback
switches. In MM2 it is created at start-up and lives for the whole run;
OpenMM2 builds a `GameInput` for each race and one for the control page.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmInput::mmInput` | ported | `controls::Options`, `GameInput`, `ForceFeedback` | the defaults: dead zone 0.1, sensitivity 1, auto reverse on, keyboard steering 1, the filters' rates 1 / 1 / 2, collision and road scales 1, the 0.5 s hold-off (+0x168) and the 0.1 s push (+0x16c); strings 271-275 read for the descriptions |
| `mmInput::~mmInput`, `mmInput::'scalar_deleting_destructor'` | replaced | C++ object lifetimes | |
| `mmInput::AttachToPipe` | ported | `controls::actions()` | the 34 slots with their strings (276-309) and types; the joystick (`mmJoyMan::Init`) and the event queue are the platform layer's (replaced) |
| `mmInput::IOInit` | ported | `controls::actions()` | slot number = event number, the type (1 press, 2 button, 4 axis, 6 either) |
| `mmInput::Reset` | ported (new) | `GameInput::reset`, `phys::ArcadeControls::reset` | the pedal values, the steering, the filters' positions; the pedal swap (+0x1d4) is ArcadeControls' |
| `mmInput::AutoSetup` | ported | frontend `ControlPage::resetDefaults` | the keyboard, force feedback off |
| `mmInput::RestoreDefaultConfig` | ported (new) | `controls::defaultBindings`, `clearBindings` | the defaults are used wherever nothing is stored; MM2 also creates an empty `mminput.cfg` (not needed) |
| `mmInput::SetDefaultConfig` | ported (new) | `controls::defaultBindings` | all five sets (see the table under mmIO); fixed the joystick's handbrake (button 1) |
| `mmInput::Init` | ported (new) | `GameInput::configure`, `slotEnabled`, `slotLocked` | keyboard: no Steering, no Camera Pan; joystick: no Steer Left / Right, steering fixed; game pad: no Steer Left / Right / Camera Pan; wheel and mouse: as the game pad with the steering fixed; the discrete steering only for the keyboard (+0x17c); the dead zone on the joystick |
| `mmInput::FlagIODevChanged` | not needed | | marks the customize list's rows for redrawing; OpenMM2 draws every frame |
| `mmInput::ReturnStateCaptured`, `mmInput::CaptureState` | ported (new) | `CaptureReader::begin`, `poll` | |
| `mmInput::BuildCaptureIO` | ported (new) | `Rebinder::capture` | keys, the left or right mouse button, joystick buttons 1-13, axes (the steering slot takes the whole axis for a half); results Assigned / Duplicate / Rejected as 1 / 2 / 0 |
| `mmInput::ForceAssignment` | ported (new) | `Rebinder::forceAssign` | |
| `mmInput::SanityCheck` | ported (new) | `Rebinder::sanityCheck` | Throttle, Brakes and Handbrake may change between button and axis; an unbound slot is checked against its default's reading (inferred: MM2 keeps the old reading) |
| `mmInput::IsAlreadyAssigned` | ported (new) | `Rebinder::alreadyAssigned`, `Rebinder::assign` | a fixed slot refuses; a listed slot is a duplicate; MM2 returns the slot's index in its device table, so the mouse set's Map Toggle (index 0) is never reported (kept) |
| `mmInput::AssignIO` | ported (new) | `Rebinder::assign` | |
| `mmInput::Update` | ported (new) | `GameInput::update`, `RaceScreen::update` | ProcessEvents then ProcessStates; the joystick is read only for the joystick types; the start-up mouse-centre fields (+0x1dc-+0x1f0) have no reader (not needed) |
| `mmInput::PollStates` | ported (new) | `CaptureReader::poll` | one newly pressed key, else a mouse button alone (latched every other frame), else for the joystick types the lowest of the first sixteen buttons, else an axis; its split piece 0x52caff is the tail after a log call |
| `mmInput::PollSuperQ` | ported (new) | `CaptureReader::poll` | exactly one key went down |
| `mmInput::Flush` | ported (new) | `GameInput::flush`, `RaceScreen::openPopup` / `closePopup` / `openChat` | |
| `mmInput::ClearEventHitFlags` | ported (new) | `GameInput::update` | each slot fires at most once a frame |
| `mmInput::ProcessMouseEvents` | ported (new) | `GameInput::processMouse` | an event (a move or a button change) with buttons held fires the slot bound to exactly that mask; OpenMM2 has one mouse event a frame |
| `mmInput::ProcessKeyboardEvents`, `mmInput::GetBufferedKeyboardData`, `mmInput::GetNextKeyboardEvent` | ported (new) | `GameInput::processKeyboard` | DirectInput's buffered presses (no repeats), read from the last; `platform::Input::keysPressedThisFrame` |
| `mmInput::ProcessEvents` | ported (new) | `GameInput::update` | |
| `mmInput::ProcessStates` | ported (new) | `GameInput::processStates` | only the slots the controller reads |
| `mmInput::ScanState` | ported (new) | `GameInput::processStates` | mouse button bits, key held, joystick button |
| `mmInput::ProcessJoyEvents` | ported (new) | `GameInput::processJoystick` | buttons fire on the way down (mmIODev +0xa0). Its branch for press slots bound to an axis half (past 0.75 every 0.8 s, compared crossed over) never runs: `mmIODev::Assign` and `SanityCheckioType` never let a press slot take an axis, so it is not ported. Split piece 0x52d4f5 is its jump table |
| `mmInput::ScanForEvent`, `mmInput::PutEventInQueue`, `mmInput::PopEvent` | ported (new) | `GameInput::scanForEvent`, `queue`, `events`, `fired` | the first matching press slot not yet hit; up to 30 a frame; popped last first |
| `mmInput::PollContinuous` | ported (new) | `GameInput::pollContinuous` | the mouse: (u x 2 - 1) / mouse sensitivity; the joystick: GetAxis. Throttle stored only for the joystick and wheel (lower end clamped), Brakes and Handbrake clamped 0-1, Steering for every controller but the keyboard. The Camera Pan slot's value is never stored: MM2 compares the slot's 64-bit mask with `(int)(1 << 31)`, a negative number (kept) |
| `mmInput::GetThrottle`, `mmInput::GetBrakes` | ported | `phys::ArcadeControls::apply` | the swap while reversing (vehicle area) |
| `mmInput::GetThrottleVal`, `mmInput::GetBrakesVal`, `mmInput::GetHandBrake` | ported (new) | `GameInput::throttle`, `brakes`, `handBrake` | a held button gives 1, an axis its value; OpenMM2's game pad beside the keyboard adds its triggers and South |
| `mmInput::FilterDiscreteSteering` | ported | `phys::SteeringFilter` via `GameInput::steering` | vehicle area; target ±1 (+0x1a8), Steer Left wins |
| `mmInput::FilterGamepadSteering` | ported | `phys::SteeringFilter` via `GameInput::steering` | its own position (+0x1a0); the target is the axis, unclamped |
| `mmInput::GetSteering` | ported (new) | `GameInput::steering`, `steeringUnfiltered` | keyboard, game pad, else `playerFilterSteering` (or the raw axis without the callback, the control page) |
| `mmInput::GetCamPan` | ported (new) | `GameInput::camPan` | fixed: the joystick controller's POV hat (north 1, east 0.75, south 0.5, west 0.25) before the look buttons; the analog slot never applies (above) |
| `mmInput::DeviceConnected` | ported | `readJoystick().present`, frontend `joystickConnected` | |
| `mmInput::JoystickHasCoolie` | ported (new) | `JoystickFrame::hasPov`, frontend POV lamp | |
| `mmInput::DoingFF` | ported (new) | `ForceFeedback::doing` | a force-feedback joystick (+0x160), the FORCE FEEDBACK option (a global switch), the joystick or wheel controller |
| `mmInput::StopAllFF` | ported (new) | `ForceFeedback::stopAll`, `RaceScreen` popups and exit | |
| `mmInput::FFPlay`, `mmInput::FFStop`, `mmInput::FFIsPlaying`, `mmInput::FFSetValues` | ported (new) | `ForceFeedback::play`, `stop`, `isPlaying`, `setValues` | gated by +0x164 (set by Init) and a force-feedback joystick |

## mmIO, mmIODev

`mmIO` is a slot (its name, event number and type); `mmIODev` is one
binding of it in one controller's set (device, component, reading). The
default sets (`mmInput::SetDefaultConfig` through `mmIO::InitDev`):

| Slot | Mouse | Keyboard | Joystick | Game pad | Wheel |
| --- | --- | --- | --- | --- | --- |
| Map Toggle | Tab | Tab | button 4 | Tab | button 5 (6+ buttons) else Tab |
| HUD Toggle | H | H | H | H | button 4 (6+) else H |
| Steering | mouse X | key 8 (not read) | X | X | X |
| Throttle / Brakes | left / right button | Up / Down | Y up / Y down | buttons 1 / 2 | Y up / Y down |
| Handbrake | Space | Space | button 1 | button 4 | button 1 |
| Change Camera | C | C | button 3 | C | button 3 (6+) else C |
| Horn | Return | Return | button 2 | button 3 | button 2 |
| Wide Angle / Dashboard | W / D | W / D | W / D | buttons 6 / 5 | W / D |
| Shift Up / Down | A / Z | A / Z | A / Z | buttons 8 / 7 | buttons 7 / 8 (8+) else A / Z |
| Rear View Mirror | Backspace | Backspace | Backspace | Backspace | button 6 (6+) else Backspace |
| Camera Pan | POV | POV (not read) | POV | POV (not read) | POV (not read) |

The rest are the keyboard's keys in every set.

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmIO::mmIO`, `mmIO::~mmIO`, `mmIO::Init` | ported | `controls::ActionInfo`, `actions()` | |
| `mmIO::InitDev` | ported (new) | `controls::defaultBindings` | |
| `mmIO::Assign` | ported (new) | `Rebinder::assign` | |
| `mmIO::operator==`, `mmIODev::operator==` | ported (new) | `GameInput::scanForEvent` | device and component (key or mouse mask) |
| `mmIO::GetDescription`, `mmIODev::GetDescription` | ported (new) | `controls::describe` | strings 271-273 and 310-324 ("Left Mouse Button", "Joy Button 3", "Joy Y-Axis Up"); the keyboard's names see `ConvertDItoString`; split piece 0x52f8a6 is its jump table |
| `mmIO::CompareComponent` | ported (new) | `sameControl` (Controls.cpp) | the same component, or a whole joystick axis against one of its halves; MM2 compares the numbers whatever the device, so DirectInput keys 10 / 11 also clash with 0x11-0x14 (OpenMM2's keys are SDL scancodes: compared only for the joystick) |
| `mmIODev::mmIODev`, `mmIODev::~mmIODev`, `mmIODev::'vector_deleting_destructor'` | ported | `controls::Binding`, `BindingSet` | |
| `mmIODev::Init`, `mmIODev::Assign` | ported (new) | `controls::assignedIoType` | a press slot takes buttons and keys only; the others also mouse axes and any other joystick component as an axis; a control the slot cannot take leaves it unchanged |
| `mmIODev::GetComponentType` | ported (new) | `controls::isAxisComponent` | split piece 0x52f936 is its tail |
| `mmIODev::SanityCheckioType` | ported (new) | `Rebinder::sanityCheck` | |
| `mmIODev::ReadBinary`, `mmIODev::WriteBinary` | replaced | `loadBindings`, `storeBinding` | MM2 saves every set in the player's configuration (`mmPlayerConfig::LoadBinary` / `SaveBinary`); OpenMM2 keeps them as text in `[Controls]` |

## mmJoyMan, mmJoystick, mmJaxis

MM2 enumerates the attached DirectInput joysticks and opens the first
(`mmJoyMan::Init`); it asks for -2000..2000 on X, Y and, with more than two
axes, Z and Rz, and 0..36000 on the POV, turns autocentring off, and sets
the dead zone on X and Y. OpenMM2's platform layer opens every device; the
race and the control page take the first one (`readJoystick`: a game pad
first for the game pad controller, else a raw joystick first, the other
kind otherwise; inferred, since SDL splits what DirectInput listed
together).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmJoyMan::mmJoyMan`, `mmJoyMan::~mmJoyMan`, `mmJoyMan::Init` | replaced | `platform::Input::openDevice`, `readJoystick` | only the first device is MM2's joystick (devices 5-7 never exist); split piece 0x52fd18 is an unwind |
| `mmJoyMan::Update` | replaced | `readJoystick` each frame | |
| `mmJoyMan::ButtonToBit`, `mmJoyMan::GetJoyButton` | ported (new) | `JoystickFrame::button` | buttons 1-12 |
| `mmJoyMan::GetJoyAxis` | ported (new) | `JoystickFrame::axis` | |
| `mmJoyMan::PollJoyButtons`, `mmJoyMan::GetOneButton`, `mmJoyMan::PollJoyAxes`, `mmJoyMan::SetCapture` | ported (new) | `CaptureReader` | |
| `mmJoyMan::GetFFEffect`, `mmJoyMan::FFPlay`, `mmJoyMan::FFStop`, `mmJoyMan::FFIsPlaying`, `mmJoyMan::FFSetValues`, `mmJoyMan::StopAllFF` | ported (new) | `ForceFeedback`, `platform::FFDevice` | numbering friction 0, collision 1, road 2, spring 3 |
| `mmJoyMan::HasCoolie` | ported (new) | `JoystickFrame::hasPov` | |
| `mmJoystick::mmJoystick`, `mmJoystick::~mmJoystick`, `mmJoystick::Init` | replaced | `platform::Input`, `platform::openForceFeedback` | a force-feedback device creates the four effects; split pieces 0x530585 / 0x5305a7 are the tails after the log calls; `??1mmJoystick@@QAE@XZ_SEH` is an unwind (not needed) |
| `mmJoystick::inputPrepareDevice` | ported (new) | `normalizeAxis`, `directInputAxis` | the range -2000..2000 (split pieces 0x530623-0x530704 are log tails) |
| `mmJoystick::SetDeadZone` | ported | `controls::applyDeadZone`, `directInputAxis` | DIPROP_DEADZONE (option x 10000) on X and Y of the first joystick, inferred rescaling; prepared with 1.0 and set from the option by `mmInput::Init` |
| `mmJoystick::DisableAutoCenter` | ported (new) | `HapticDevice` (SDL_SetHapticAutocenter 0) | |
| `mmJoystick::Poll` | replaced | `readJoystick` | X, Y, Z, Rz (raw joystick axes 0-3; a game pad shows DirectInput's XInput view: left stick X / Y, Z = left trigger - right trigger, no Rz; inferred), the first hat, the buttons |
| `mmJoystick::GetAxis` | ported (new) | `JoystickFrame::axis` | Z negated, U and V never polled (0), halves as positive values |
| `mmJoystick::GetButton`, `mmJoystick::GetNumButtons` | ported (new) | `JoystickFrame::button`, `numButtons` | |
| `mmJoystick::Update`, `mmJoystick::ResetAxisCapture` | ported (new) | `CaptureReader` | X and Y by halves (right 0x12, left 0x11, down 0x14, up 0x13), Z 0xc, Rz 0xe |
| `mmJoystick::InputCreateEffect`, `mmJoystick::InputInitEffect` | replaced | `HapticDevice::HapticDevice` | `?InputCreateEffect@mmJoystick@@QAEXXZ_SEH` is an unwind (not needed) |
| `mmJoystick::InputStopEffect`, `mmJoystick::GetFFEffect` | ported (new) | `ForceFeedback::stopAll`, `platform::FFEffect` | |
| `mmJoystick::PrintDeviceCaps` | not needed | | log output (its split pieces 0x530f21-0x5310ba too) |
| `mmJaxis::mmJaxis`, `mmJaxis::~mmJaxis` | ported | `JoystickFrame` | |
| `mmJaxis::SetRange`, `mmJaxis::Normalize` | ported (new) | `normalizeAxis` | ((v - min) x 2) / range - 1 |
| `mmJaxis::NormalizePOV` | ported (new) | `povFromHat` | (36000 - angle) / 36000, centred -1 |
| `mmJaxis::Capture`, `mmJaxis::ResetCapture` | ported (new) | `axisCaptured`, `CaptureReader::begin` | 0.125 either way from rest |

## Force feedback: mmEffectFF, mmFrictionFF, mmCollideFF, mmRoadFF, mmSpringFF, mmCarRoadFF, mmPlayer

`mmPlayer::UpdateFF` runs while `mmInput::DoingFF`: the friction (0.7 x the
front left tyre's friction) and the spring (0 at 10 m/s to 1 at 80 m/s
while the front left tyre grips, sent when it moves by 0.01) play all the
time; the road wave follows the surface's bumps (height at speed / width
per second) or, for a damaged car on the ground, 0.4 x damage at speed /
(radius x 10), 1-20 Hz (`mmCarRoadFF`); a wheel's suspension compressing
faster than 1.3 m/s above 5 mph gives a 0.2 push in a random direction,
and `FFImpactCallback` a push of total / 100 (0.1-1) for damaging impacts
above 5 mph, at most every 0.5 s after the last impact. The COLLISION
intensity scales the push, the ROAD FORCE the road wave and, truncated to
a whole number, the spring. SDL plays them as haptic effects; a pad with
only rumble motors shows the road and the pushes as rumble (OpenMM2
stand-in; friction and the spring have no rumble equivalent).

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `mmPlayer::UpdateFF` | ported (new) | `ForceFeedback::update`, `ffCarState`, `RaceScreen::updateForceFeedback` | vehCarSim +0x248 / +0x24c, vehCarDamage's fraction, the front left wheel's +0x674 / +0x688 / +0x690 / +0x694 / +0x6f8 / +0x6fc, each wheel's suspension speed; +0x235c is never written, so the bump test only skips at a standstill (kept); x87 products before truncation computed in double |
| `mmPlayer::FFImpactCallback` | ported (new) | `ForceFeedback::impact`, `RaceScreen::playerImpact` | the impact's summed value (vehDamageImpactInfo +0x38), damaging impacts only (vehCarDamage::ApplyImpact) |
| `mmPlayer::ResetFF` | ported (new) | `ForceFeedback::reset`, `update(paused)` | |
| `mmEffectFF::mmEffectFF`, `mmEffectFF::~mmEffectFF`, `mmEffectFF::Play`, `mmEffectFF::Stop`, `mmEffectFF::SetValues` | replaced | `platform::FFDevice` | the base class (no effect) |
| `mmFrictionFF::Init` | replaced | `HapticDevice` | SDL_HAPTIC_FRICTION, X axis, infinite |
| `mmFrictionFF::SetValues`, `mmFrictionFF::Assign` | ported (new) | `ForceFeedback::setValues` | coefficient x 10000, 0-10000, both sides, saturation 10000 |
| `mmFrictionFF::Play`, `mmFrictionFF::Stop` | ported (new) | `ForceFeedback::play`, `stop` | always allowed |
| `mmSpringFF::Init` | replaced | `HapticDevice` | SDL_HAPTIC_SPRING |
| `mmSpringFF::SetValues`, `mmSpringFF::Assign` | ported (new) | `ForceFeedback::setValues` | trunc(road force) x strength x 10000 |
| `mmSpringFF::Play`, `mmSpringFF::Stop` | ported (new) | `ForceFeedback::play`, `stop` | only with a road force |
| `mmRoadFF::Init` | replaced | `HapticDevice` | the first periodic type (square, else sine), X axis, infinite, magnitude 2000, period 5 s |
| `mmRoadFF::SetValues`, `mmRoadFF::Assign` | ported (new) | `ForceFeedback::setValues` | period x 10^6 us, magnitude x 10000 x road force |
| `mmRoadFF::Play`, `mmRoadFF::Stop` | ported (new) | `ForceFeedback::play`, `stop` | |
| `mmCollideFF::Init` | replaced | `HapticDevice` | periodic on X and Y in polar form, 0.1 s, period 2 s; split pieces 0x531747-0x53178d are the error-log tails |
| `mmCollideFF::SetValues`, `mmCollideFF::Assign` | ported (new) | `ForceFeedback::setValues` | gain = strength x 10000 x collision intensity, direction 0-360 degrees (only gain and direction are sent) |
| `mmCollideFF::Play`, `mmCollideFF::Stop` | ported (new) | `ForceFeedback::play`, `stop` | Play does not mark it playing; Stop does nothing |
| `mmCarRoadFF::mmCarRoadFF`, `mmCarRoadFF::~mmCarRoadFF`, `mmCarRoadFF::'scalar_deleting_destructor'` | ported (new) | `ForceFeedback` members | frequency 1-20, period and magnitude 1 |
| `mmCarRoadFF::AssignProperties`, `mmCarRoadFF::UpdateVals` | ported (new) | `ForceFeedback::start`, `roadUpdateVals` | sent only when a value changed |
| `mmCarRoadFF::SetFGVals`, `mmCarRoadFF::Start`, `mmCarRoadFF::Stop`, `mmCarRoadFF::IsPlaying` | ported (new) | `ForceFeedback::roadSetFG`, `roadStart`, `roadStop`, `isPlaying` | |
| `mmCarRoadFF::Reset` | ported (new) | `ForceFeedback::reset` | |
| `mmCarRoadFF::Update` | not needed | | it would play a sound (+0x18) once a period while +0x1c is 1, but +0x1c is only ever set to 0 (constructor, AssignProperties) and +0x18 is never set, so no sound plays (the audio audit's "road sound" never runs) |
| `vehFeedback::vehFeedback`, `vehFeedback::~vehFeedback`, `vehFeedback::'scalar_deleting_destructor'`, `vehFeedback::SetFeedback`, `vehFeedback::SetTimingUnit`, `vehFeedback::GetNumActuators`, `vehFeedback::SetActuatorValue`, `vehFeedback::PlayFeedbackSample`, `vehFeedback::PlayFeedbackSampleID`, `vehFeedback::ClearAllSamples`, `vehFeedback::GetNextUnit`, `vehFeedback::Update` | not needed | | a console pad's rumble timeline that vehCar creates; on the PC its pad is never set (`SetPadID` has no caller) and `GetNumActuators` returns 0, so it does nothing |

## The steering: playerFilterSteering, mmMouseSteerBar

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `playerFilterSteering` | ported | `GameInput::steering` -> `AnalogSteering::filter` | `mmPlayer::FilterSteering` for the mouse, joystick and wheel (session area's port) |
| `mmMouseSteerBar::Init`, `mmMouseSteerBar::Update`, `mmMouseSteerBar::Cull`, `mmMouseSteerBar::'scalar_deleting_destructor'` | ported (new) | frontend `ControlPage::drawSteeringBar` | `mouse_bar` at (0.1, 0.85) of the screen, `mouse_ar` 16 px above at (bar width / 2 - 15) x (1 + steering) + a quarter of its width; the value is `GetSteering` without the callback (`ControlSetup::Update`); split piece 0x534440 is the base destructor |

The race's own bar is `mmExternalView::Cull` (the HUD's class): with the
mouse controller it draws `mouse_bar` centred two bar heights above the
bottom and `mouse_ar` 16 px above it at (bar width / 2 - 15) x the steering
the car was given (mmPlayer +0x2264, the recorded value), before the
gauges. This audit added it to `Hud::drawCluster` with the steering in
`PlayerState::mouseSteer` (set by RaceScreen).

## The replay recording (mmReplayManager)

`mmReplayManager::Update` reads `mmInput::GetSteering(playerFilterSteering)`,
`GetThrottle`, `GetBrakes` and `GetHandBrake` every frame and stores them
as bytes (steering ftol(x 127) signed, the pedals ftol(x 255)); the game
(`mmGame::UpdateSteeringBrakes`, a child of the replay manager) drives the
car with `mmReplayManager::GetSteering` / `GetThrottle` / `GetBrakes` /
`GetHandBrakes` (x 1/127, x 1/255). OpenMM2 applies the same quantisation
(`controls::replayQuantize`, RaceScreen::updatePlayer); recording and
playing replays is the game-flow area's.

## Devices and events: ioInput, ioKeyboard, ioMouse, ioJoystick, ioPad, ioEventQueue, eq*

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ioInput::Begin`, `ioInput::End`, `ioInput::Poll`, `ioInput::Update` | replaced | `platform::Input::beginFrame`, `handleEvent`, `updateDevices` | |
| `ioKeyboard::Begin`, `ioKeyboard::End`, `ioKeyboard::Update` | replaced | `platform::Input` (SDL key events by scancode) | DirectInput's two key-state buffers |
| `ioKeyboard::GetBufferedInput` | replaced | `platform::Input::keysPressedThisFrame` | presses without repeats; also the chat's text (SDL text input) |
| `ioMouse::Begin`, `ioMouse::End`, `ioMouse::Update` | replaced | `platform::Input` (mouse position, buttons, wheel) | split pieces 0x4bb480-0x4bb4b0 are the static `ioPad::PADS` constructor and destructor registration |
| `ioJoystick::Begin`, `ioJoystick::BeginAll`, `ioJoystick::End`, `ioJoystick::EndAll`, `ioJoystick::EnumDeviceProc`, `ioJoystick::EnumObjectProc`, `ioJoystick::Poll`, `ioJoystick::PollAll`, `ioJoystick::Update`, `ioJoystick::UpdateAll` | not needed | | `Main` clears `ioInput::bUseJoystick`, so the older joystick layer never starts (the game's joystick is mmJoyMan's) |
| `ioPad::ioPad`, `ioPad::~ioPad`, `ioPad::Begin`, `ioPad::BeginAll`, `ioPad::End`, `ioPad::EndAll`, `ioPad::Update`, `ioPad::UpdateAll` | not needed | | a console pad built from the keymap and ioJoystick each frame; with no ioJoystick and nothing reading `ioPad::PADS` (only vehFeedback, whose pad is never set) it changes nothing |
| `ioEventQueue::Pop`, `ioEventQueue::Queue` | replaced | SDL's event queue (`platform::pollEvents`) | the window procedure's 32-entry queue |
| `eqEventHandler::eqEventHandler`, `eqEventHandler::~eqEventHandler`, `eqEventHandler::'vector_deleting_destructor'`, `eqEventHandler::AddClient`, `eqEventHandler::RemoveClient` | replaced | `platform::Input`, `ui::NavReader` | the "SuperQ" and its eight clients |
| `eqEventHandler::Update` | replaced | `platform::Input::handleEvent` | the ioEvents to mouse (buttons left 1, right 2, middle 4) and keyboard calls with the Ctrl / Shift / Alt bits; split piece 0x4a1bd6 is its jump tables (with `IO_EVENT_INPUT`, `IO_EVENT_KEYDOWN`) |
| `eqEventHandler::SetActive`, `eqEventHandler::ClearActive` | not needed | | empty |
| `eqEventMonitor::eqEventMonitor`, `eqEventMonitor::~eqEventMonitor`, `eqEventMonitor::'scalar_deleting_destructor'` | replaced | `platform::Input` | |
| `eqEventMonitor::Mouse`, `eqEventMonitor::Keyboard` | not needed | | log output when the handler's debug flag is set (split pieces 0x4a2183 / 0x4a21fe are the tails) |
| `eqEventQ::eqEventQ`, `eqEventQ::~eqEventQ`, `eqEventQ::'vector_deleting_destructor'`, `eqEventQ::Mouse`, `eqEventQ::Keyboard`, `eqEventQ::Queue`, `eqEventQ::Pop` | replaced | `platform::Input`, `ui::NavReader`, `GameInput::processMouse` | the menus' and mmInput's event rings |

## Free functions handed over by the infrastructure triage

| MM2 | Status | OpenMM2 | Notes |
| --- | --- | --- | --- |
| `ConvertDItoString` | replaced | `platform::keyName` (SDL_GetScancodeName) | MM2 asks Windows (GetKeyNameText) for the key's name in the keyboard layout's language; OpenMM2 shows SDL's English names (deviation) |
| `AngelReadKeyString` | not needed | | key names from DI_KEYS.DLL, used only when string 275 is non-zero; it is "0" in the retail string table |

midtown2.exe's command-line options have none for input but `-noime`
(the Windows IME for the chat line; SDL's text input replaces it), so
there is nothing to wire into `app/CommandLine`.

## Unreachable functions

The coverage tool finds no path from the entry point to these: the
config-file readers and writers (`mmInput::LoadConfig`,
`mmInput::SaveConfig`, `mmInput::BinaryLoadConfig`,
`mmInput::BinarySaveConfig`, `mmInput::SaveCB`, `mmInput::SaveCodeConfig`,
`mmInput::PrintIODev`, `mmIO::Read`, `mmIO::Write`, `mmIODev::Read`,
`mmIODev::Write`, `mmIODev::Print`), stubs and setters (`mmInput::EventToButton`,
`mmInput::GamepadConnected`, `mmInput::JoystickConnected`,
`mmInput::WheelConnected`, `mmInput::WheelHas3Axis`,
`mmInput::JoystickHasThrottle`, `mmInput::SetDeadZone`,
`mmInput::SetForceFeedbackScale`, `mmInput::SetRoadForceScale` (the page
writes +0x1ac / +0x1b0 directly; OpenMM2 clamps in `Options::load`),
`mmInput::ToggleFFEnabled`, `mmJoyMan::HasThrottle`, `mmJoyMan::QJoystick`,
`mmJoyMan::ToggleEnabled`, `mmIO::Clear`, `mmIO::ForceSettingAcrossConfigs`),
the old effect helpers (`mmJoystick::SetShake`, `PlayShake`, `StopShake`,
`PlayCollision`, `SetSteer`, `PlaySteer`, `StopSteer`, `SetFriction`,
`PlayFriction`, `StopFriction`), `eqEventHandler::BeginGfx`, `EndGfx`,
`MinimizeApp`, `RestoreApp`, `EKeyName`, `ioEventQueue::Command`,
`ioEventQueue::Peek`, `vehFeedback::GetPad`, `SetPad`, `GetPadID`,
`SetPadID`, and the vector deleting destructors of `mmIO`, `mmIODev`
(scalar) and `mmJoystick`. Not needed.

## For other areas

- HUD (hud-views): the race's mouse steering bar belongs to
  `mmExternalView::Cull`; this audit draws it in `Hud::drawCluster` from
  `PlayerState::mouseSteer`. If the HUD area draws it too, one of the two
  should go (the HUD's needs only the steering the car was given and the
  controller being the mouse).
- Audio: `mmCarRoadFF::Update`'s sound never plays (above).
- Game-flow: the replay recording itself (`mmReplayManager`); the input
  quantisation it implies is applied by `controls::replayQuantize`.
- `mmGame::UpdateGameInput`'s use of the camera pan in the dashboard view
  (the HUD hidden while panning, the POV camera turned by pan x 2 pi) is
  the camera / session areas'; `GameInput::camPan` now includes the POV hat.
