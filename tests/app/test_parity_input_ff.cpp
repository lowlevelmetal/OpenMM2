// Parity checks of the input devices, bindings and their per-frame reading
// against MM2's mmInput / mmIO / mmIODev / mmJoystick (MM2Recomp, build
// 3393). See docs/parity/mm2/input-ff.md.

#include "app/Controls.h"
#include "app/GameInput.h"
#include "core/Ini.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace mm2;
using namespace mm2::app::controls;
using platform::Key;

namespace {

Binding at(const BindingSet& s, Action a) { return s[static_cast<std::size_t>(a)]; }

InputFrame joyFrame(float x, float y, std::uint32_t buttons = 0, float pov = -1.0f) {
    InputFrame f;
    f.joy.present = true;
    f.joy.x = x;
    f.joy.y = y;
    f.joy.buttons = buttons;
    f.joy.pov = pov;
    f.joy.numButtons = 8;
    return f;
}

GameInput make(Controller c, int buttons = 8) {
    GameInput g;
    Options o;
    o.controller = c;
    g.configure(c, defaultBindings(c, buttons), o, true);
    return g;
}

} // namespace

TEST(ParityInputFF, SlotKindsFollowAttachToPipe) {
    EXPECT_EQ(info(Action::MapToggle).kind, SlotKind::Event);
    EXPECT_EQ(info(Action::Steering).kind, SlotKind::Axis);
    EXPECT_EQ(info(Action::SteerLeft).kind, SlotKind::Button);
    EXPECT_EQ(info(Action::Throttle).kind, SlotKind::ButtonOrAxis);
    EXPECT_EQ(info(Action::Handbrake).kind, SlotKind::Button);
    EXPECT_EQ(info(Action::Horn).kind, SlotKind::ButtonOrAxis);
    EXPECT_EQ(info(Action::LookBack).kind, SlotKind::ButtonOrAxis);
    EXPECT_EQ(info(Action::CameraPan).kind, SlotKind::Axis);
    EXPECT_EQ(info(Action::EnterChat).kind, SlotKind::Event);
}

TEST(ParityInputFF, DefaultSetsFollowSetDefaultConfig) {
    using component::joyButton;
    const BindingSet mouse = defaultBindings(Controller::Mouse, -1);
    EXPECT_EQ(at(mouse, Action::Steering), Binding::mouse(component::kMouseX));
    EXPECT_EQ(at(mouse, Action::Throttle), Binding::mouse(component::kMouseLeft));
    EXPECT_EQ(at(mouse, Action::Brakes), Binding::mouse(component::kMouseRight));
    EXPECT_EQ(at(mouse, Action::Handbrake), Binding::key(Key::Space));

    const BindingSet keys = defaultBindings(Controller::Keyboard, -1);
    EXPECT_EQ(at(keys, Action::Throttle), Binding::key(Key::Up));
    EXPECT_EQ(at(keys, Action::Steering), Binding::key(Key::Num8)); // never read
    EXPECT_EQ(at(keys, Action::RotatingMap), Binding::key(Key::F));

    // The joystick's handbrake is its first button (0x15), not the sixth.
    const BindingSet joy = defaultBindings(Controller::Joystick, 8);
    EXPECT_EQ(at(joy, Action::Handbrake), Binding::joy(joyButton(1)));
    EXPECT_EQ(at(joy, Action::Horn), Binding::joy(joyButton(2)));
    EXPECT_EQ(at(joy, Action::ChangeCamera), Binding::joy(joyButton(3)));
    EXPECT_EQ(at(joy, Action::MapToggle), Binding::joy(joyButton(4)));
    EXPECT_EQ(at(joy, Action::Throttle), Binding::joy(component::kJoyYUp));
    EXPECT_EQ(at(joy, Action::Brakes), Binding::joy(component::kJoyYDown));
    EXPECT_EQ(at(joy, Action::CameraPan), Binding::joy(component::kJoyPov));

    const BindingSet pad = defaultBindings(Controller::GamePad, 10);
    EXPECT_EQ(at(pad, Action::Throttle), Binding::joy(joyButton(1)));
    EXPECT_EQ(at(pad, Action::Brakes), Binding::joy(joyButton(2)));
    EXPECT_EQ(at(pad, Action::Horn), Binding::joy(joyButton(3)));
    EXPECT_EQ(at(pad, Action::Handbrake), Binding::joy(joyButton(4)));
    EXPECT_EQ(at(pad, Action::Dashboard), Binding::joy(joyButton(5)));
    EXPECT_EQ(at(pad, Action::WideAngle), Binding::joy(joyButton(6)));
    EXPECT_EQ(at(pad, Action::ShiftDown), Binding::joy(joyButton(7)));
    EXPECT_EQ(at(pad, Action::ShiftUp), Binding::joy(joyButton(8)));
    EXPECT_EQ(at(pad, Action::ChangeCamera), Binding::key(Key::C));

    // The wheel: buttons 3-6 with six buttons, 7-8 (up, down) with eight.
    const BindingSet small = defaultBindings(Controller::Wheel, 4);
    EXPECT_EQ(at(small, Action::ChangeCamera), Binding::key(Key::C));
    EXPECT_EQ(at(small, Action::ShiftUp), Binding::key(Key::A));
    const BindingSet six = defaultBindings(Controller::Wheel, 6);
    EXPECT_EQ(at(six, Action::ChangeCamera), Binding::joy(joyButton(3)));
    EXPECT_EQ(at(six, Action::HudToggle), Binding::joy(joyButton(4)));
    EXPECT_EQ(at(six, Action::MapToggle), Binding::joy(joyButton(5)));
    EXPECT_EQ(at(six, Action::RearViewMirror), Binding::joy(joyButton(6)));
    EXPECT_EQ(at(six, Action::ShiftDown), Binding::key(Key::Z));
    const BindingSet eight = defaultBindings(Controller::Wheel, 8);
    EXPECT_EQ(at(eight, Action::ShiftUp), Binding::joy(joyButton(7)));
    EXPECT_EQ(at(eight, Action::ShiftDown), Binding::joy(joyButton(8)));
    EXPECT_EQ(at(eight, Action::Handbrake), Binding::joy(joyButton(1)));
}

TEST(ParityInputFF, InitEnablesAndLocksSlots) {
    EXPECT_FALSE(slotEnabled(Controller::Keyboard, Action::Steering));
    EXPECT_FALSE(slotEnabled(Controller::Keyboard, Action::CameraPan));
    EXPECT_TRUE(slotEnabled(Controller::Keyboard, Action::SteerLeft));
    EXPECT_FALSE(slotEnabled(Controller::Joystick, Action::SteerRight));
    EXPECT_TRUE(slotEnabled(Controller::Joystick, Action::CameraPan));
    EXPECT_FALSE(slotEnabled(Controller::GamePad, Action::CameraPan));
    EXPECT_FALSE(slotEnabled(Controller::Wheel, Action::CameraPan));
    EXPECT_TRUE(slotLocked(Controller::Joystick, Action::Steering));
    EXPECT_TRUE(slotLocked(Controller::Mouse, Action::Steering));
    EXPECT_FALSE(slotLocked(Controller::GamePad, Action::Steering));
    EXPECT_TRUE(slotListed(Controller::GamePad, Action::Steering));
    EXPECT_FALSE(slotListed(Controller::Wheel, Action::Steering));
}

TEST(ParityInputFF, AssignGivesTheReading) {
    // mmIODev::Assign.
    EXPECT_EQ(assignedIoType(SlotKind::Event, Binding::key(Key::A)), IoType::Event);
    EXPECT_EQ(assignedIoType(SlotKind::Event, Binding::joy(component::kJoyX)), IoType::None);
    EXPECT_EQ(assignedIoType(SlotKind::Event, Binding::mouse(component::kMouseX)), IoType::None);
    EXPECT_EQ(assignedIoType(SlotKind::ButtonOrAxis, Binding::joy(component::kJoyYUp)), IoType::Axis);
    EXPECT_EQ(assignedIoType(SlotKind::ButtonOrAxis, Binding::joy(component::joyButton(2))), IoType::Held);
    EXPECT_EQ(assignedIoType(SlotKind::Axis, Binding::mouse(component::kMouseX)), IoType::Axis);
    EXPECT_EQ(assignedIoType(SlotKind::Button, Binding::mouse(component::kMouseLeft)), IoType::Held);
}

TEST(ParityInputFF, BindingsAreStoredPerController) {
    EXPECT_EQ(bindingKey(Controller::Keyboard, 280), "Bind.280");
    EXPECT_EQ(bindingKey(Controller::Wheel, 280), "Bind.Wheel.280");
    for (const Binding b : {Binding::key(Key::Tab), Binding::mouse(component::kMouseRight),
                            Binding::joy(component::kJoyYDown), Binding::joy(component::joyButton(12)),
                            Binding::joy(component::kJoyPov), Binding{}}) {
        const auto parsed = parseBinding(bindingText(b));
        ASSERT_TRUE(parsed.has_value()) << bindingText(b);
        EXPECT_EQ(*parsed, b) << bindingText(b);
    }
    EXPECT_FALSE(parseBinding("Joy Button 99").has_value());

    IniFile ini;
    storeBinding(ini, Controller::Joystick, Action::Horn, Binding::joy(component::joyButton(7)));
    ini.set("Controls", bindKey(280), platform::keyName(Key::W)); // the keyboard's throttle
    const BindingSet joy = loadBindings(ini, Controller::Joystick, 8);
    EXPECT_EQ(at(joy, Action::Horn), Binding::joy(component::joyButton(7)));
    EXPECT_EQ(at(joy, Action::Throttle), Binding::joy(component::kJoyYUp)); // the keyboard's is separate
    const BindingSet keys = loadBindings(ini, Controller::Keyboard, -1);
    EXPECT_EQ(at(keys, Action::Throttle), Binding::key(Key::W));
    clearBindings(ini, Controller::Joystick);
    EXPECT_EQ(at(loadBindings(ini, Controller::Joystick, 8), Action::Horn), Binding::joy(component::joyButton(2)));
}

TEST(ParityInputFF, DescriptionsFollowGetDescription) {
    const auto strings = [](std::uint32_t, const char* fallback) { return std::string(fallback); };
    EXPECT_EQ(describe(Binding::mouse(component::kMouseLeft), strings), "Left Mouse Button");
    EXPECT_EQ(describe(Binding::joy(component::joyButton(3)), strings), "Joy Button 3");
    EXPECT_EQ(describe(Binding::joy(component::kJoyXLeft), strings), "Joy X-Axis Left");
    EXPECT_EQ(describe(Binding::joy(component::kJoyPov), strings), "Joy POV-Axis");
    EXPECT_EQ(describe(Binding{}, strings), "UNDEFINED");
}

TEST(ParityInputFF, CaptureFollowsBuildCaptureIO) {
    BindingSet set = defaultBindings(Controller::Joystick, 8);
    Rebinder r(set, Controller::Joystick);
    Action other = Action::Count;
    // A joystick button to a press slot.
    EXPECT_EQ(r.capture(Action::WideAngle, {Captured::Kind::JoyButton, 6}), CaptureResult::Assigned);
    EXPECT_EQ(at(set, Action::WideAngle), Binding::joy(component::joyButton(6)));
    // An axis cannot be a press.
    EXPECT_EQ(r.capture(Action::Reverse, {Captured::Kind::JoyAxis, component::kJoyYUp}), CaptureResult::Rejected);
    // A button already used by a listed slot: a duplicate until forced.
    EXPECT_EQ(r.capture(Action::Reverse, {Captured::Kind::JoyButton, 2}, &other), CaptureResult::Duplicate);
    EXPECT_EQ(other, Action::Horn);
    r.forceAssign(Action::Reverse);
    EXPECT_EQ(at(set, Action::Reverse), Binding::joy(component::joyButton(2)));
    EXPECT_EQ(at(set, Action::Horn), Binding{});
    // The throttle may change from an axis half to a button, and back.
    EXPECT_EQ(r.capture(Action::Throttle, {Captured::Kind::JoyButton, 8}), CaptureResult::Assigned);
    EXPECT_EQ(at(set, Action::Throttle), Binding::joy(component::joyButton(8)));
    // A half of X clashes with the fixed steering axis.
    EXPECT_EQ(r.capture(Action::Brakes, {Captured::Kind::JoyAxis, component::kJoyXRight}), CaptureResult::Rejected);
    // Mouse buttons: the left or the right alone.
    EXPECT_EQ(r.capture(Action::Horn, {Captured::Kind::Mouse, 3}), CaptureResult::Rejected);
    EXPECT_EQ(r.capture(Action::Horn, {Captured::Kind::Mouse, 1}), CaptureResult::Assigned);

    // The game pad's steering takes the whole axis for a captured half.
    BindingSet pad = defaultBindings(Controller::GamePad, 10);
    Rebinder rp(pad, Controller::GamePad);
    EXPECT_EQ(rp.capture(Action::Steering, {Captured::Kind::JoyAxis, component::kJoyYDown}), CaptureResult::Assigned);
    EXPECT_EQ(at(pad, Action::Steering), Binding::joy(component::kJoyY));
    // A key is not an axis.
    EXPECT_EQ(rp.capture(Action::Steering, {Captured::Kind::Key, static_cast<int>(Key::G)}), CaptureResult::Rejected);

    // mmJaxis::Capture: 0.125 from the resting place.
    EXPECT_EQ(axisCaptured(0.1f, 0.2f), 0);
    EXPECT_EQ(axisCaptured(0.1f, 0.3f), 1);
    EXPECT_EQ(axisCaptured(0.0f, -0.2f), -1);
}

TEST(ParityInputFF, JoystickAxesAreDirectInputs) {
    EXPECT_FLOAT_EQ(normalizeAxis(2000), 1.0f);
    EXPECT_FLOAT_EQ(normalizeAxis(-2000), -1.0f);
    EXPECT_FLOAT_EQ(normalizeAxis(0), 0.0f);
    EXPECT_FLOAT_EQ(directInputAxis(0.05f, 0.1f), 0.0f);
    EXPECT_NEAR(directInputAxis(0.55f, 0.1f), 0.5f, 1e-3f);
    EXPECT_FLOAT_EQ(povFromHat(0x1), 1.0f);  // north
    EXPECT_FLOAT_EQ(povFromHat(0x2), 0.75f); // east
    EXPECT_FLOAT_EQ(povFromHat(0x4), 0.5f);
    EXPECT_FLOAT_EQ(povFromHat(0x8), 0.25f);
    EXPECT_FLOAT_EQ(povFromHat(0x3), 0.875f);
    EXPECT_FLOAT_EQ(povFromHat(0x0), -1.0f);
    JoystickFrame j;
    j.x = -0.5f;
    j.y = 0.25f;
    j.z = 0.3f;
    EXPECT_FLOAT_EQ(j.axis(component::kJoyXLeft), 0.5f);
    EXPECT_FLOAT_EQ(j.axis(component::kJoyXRight), 0.0f);
    EXPECT_FLOAT_EQ(j.axis(component::kJoyYDown), 0.25f);
    EXPECT_FLOAT_EQ(j.axis(component::kJoyZ), -0.3f);
    EXPECT_FLOAT_EQ(j.axis(component::kJoyU), 0.0f);
}

TEST(ParityInputFF, KeyboardStatesAndPresses) {
    GameInput g = make(Controller::Keyboard);
    InputFrame f;
    f.keysDown.set(static_cast<std::size_t>(Key::Up));
    f.keysDown.set(static_cast<std::size_t>(Key::Return));
    f.keysPressed = {Key::Tab, Key::C};
    g.update(f, 0.016f);
    EXPECT_FLOAT_EQ(g.throttle(), 1.0f);
    EXPECT_TRUE(g.held(Action::Horn));
    EXPECT_TRUE(g.fired(Action::MapToggle));
    EXPECT_TRUE(g.fired(Action::ChangeCamera));
    // PopEvent pops the last queued first; the keys were read from the last.
    ASSERT_EQ(g.events().size(), 2u);
    EXPECT_EQ(g.events()[0], Action::ChangeCamera);
    // Steer Left wins; the keyboard filter moves towards full lock.
    f.keysPressed.clear();
    f.keysDown.set(static_cast<std::size_t>(Key::Left));
    f.keysDown.set(static_cast<std::size_t>(Key::Right));
    g.update(f, 0.016f);
    EXPECT_LT(g.steering(0.1f), 0.0f);
    EXPECT_FALSE(g.fired(Action::MapToggle));
}

TEST(ParityInputFF, JoystickButtonsFireOnPress) {
    GameInput g = make(Controller::Joystick);
    // Button 3 (Change Camera) goes down: one action, not again while held.
    g.update(joyFrame(0, 0, 1u << 2), 0.016f);
    EXPECT_TRUE(g.fired(Action::ChangeCamera));
    g.update(joyFrame(0, 0, 1u << 2), 0.016f);
    EXPECT_FALSE(g.fired(Action::ChangeCamera));
    g.update(joyFrame(0, 0, 0), 0.016f);
    g.update(joyFrame(0, 0, 1u << 2), 0.016f);
    EXPECT_TRUE(g.fired(Action::ChangeCamera));
    // Button 1 holds the handbrake; Y forward is the throttle, back the brakes.
    g.update(joyFrame(0.0f, -0.6f, 1u), 0.016f);
    EXPECT_FLOAT_EQ(g.handBrake(), 1.0f);
    EXPECT_FLOAT_EQ(g.throttle(), 0.6f);
    EXPECT_FLOAT_EQ(g.brakes(), 0.0f);
    g.update(joyFrame(0.0f, 0.4f), 0.016f);
    EXPECT_FLOAT_EQ(g.throttle(), 0.0f);
    EXPECT_FLOAT_EQ(g.brakes(), 0.4f);
    // X is the steering axis; the arrow keys are not read.
    InputFrame f = joyFrame(0.3f, 0.0f);
    f.keysDown.set(static_cast<std::size_t>(Key::Left));
    g.update(f, 0.016f);
    EXPECT_FLOAT_EQ(g.steeringAxis(), 0.3f);
    EXPECT_FALSE(g.held(Action::SteerLeft));
    // Flush: a button held through it waits for the next press.
    g.update(joyFrame(0, 0, 1u << 3), 0.016f); // Map Toggle (button 4)
    EXPECT_TRUE(g.fired(Action::MapToggle));
    g.flush();
    EXPECT_FALSE(g.fired(Action::MapToggle));
}

TEST(ParityInputFF, PressSlotsNeverHoldAnAxisHalf) {
    // ProcessJoyEvents' axis-half branch never runs: a press slot cannot
    // take an axis (mmIODev::Assign), so the stick does nothing there.
    GameInput g;
    Options o;
    BindingSet set = defaultBindings(Controller::Joystick, 8);
    set[static_cast<std::size_t>(Action::ShiftUp)] = Binding::joy(component::kJoyXLeft);
    g.configure(Controller::Joystick, set, o, true);
    EXPECT_EQ(g.ioType(Action::ShiftUp), IoType::None);
    g.update(joyFrame(-1.0f, 0.0f), 1.0f);
    EXPECT_FALSE(g.fired(Action::ShiftUp));
}

TEST(ParityInputFF, MouseSteersAcrossTheWindow) {
    GameInput g = make(Controller::Mouse);
    InputFrame f;
    f.mouseX = 0.75f;
    f.mouseButtons = 1; // the left button: throttle
    g.update(f, 0.016f);
    EXPECT_FLOAT_EQ(g.steeringAxis(), 0.5f); // (0.75 x 2 - 1) / sensitivity 1
    EXPECT_FLOAT_EQ(g.throttle(), 1.0f);
    EXPECT_FLOAT_EQ(g.brakes(), 0.0f);
}

TEST(ParityInputFF, GamePadDrivesWithButtons) {
    GameInput g = make(Controller::GamePad, 10);
    InputFrame f = joyFrame(-0.5f, -1.0f, 0b1010u); // buttons 2 and 4
    g.update(f, 0.016f);
    EXPECT_FLOAT_EQ(g.throttle(), 0.0f); // Y is not the pad's throttle
    EXPECT_FLOAT_EQ(g.brakes(), 1.0f);
    EXPECT_FLOAT_EQ(g.handBrake(), 1.0f);
    EXPECT_FLOAT_EQ(g.steeringAxis(), -0.5f);
    EXPECT_LT(g.steering(0.1f), 0.0f); // FilterGamepadSteering
}

TEST(ParityInputFF, CamPanReadsThePovForTheJoystick) {
    GameInput joy = make(Controller::Joystick);
    joy.update(joyFrame(0, 0, 0, 0.75f), 0.016f);
    EXPECT_FLOAT_EQ(joy.camPan(), 0.75f);
    GameInput pad = make(Controller::GamePad, 10);
    pad.update(joyFrame(0, 0, 0, 0.75f), 0.016f);
    EXPECT_FLOAT_EQ(pad.camPan(), 0.0f);
    InputFrame f;
    f.keysDown.set(static_cast<std::size_t>(Key::Kp2));
    f.keysDown.set(static_cast<std::size_t>(Key::Kp4));
    GameInput keys = make(Controller::Keyboard);
    keys.update(f, 0.016f);
    EXPECT_FLOAT_EQ(keys.camPan(), 0.375f); // back and left
}

TEST(ParityInputFF, JoystickTypesFallBackToTheKeyboard) {
    // mmPlayerConfig::SetControls: no joystick, the keyboard.
    GameInput g;
    Options o;
    o.controller = Controller::Wheel;
    g.configure(Controller::Wheel, defaultBindings(Controller::Wheel, -1), o, false);
    EXPECT_EQ(g.controller(), Controller::Keyboard);
    EXPECT_EQ(g.binding(Action::Throttle), Binding::key(Key::Up));
}

TEST(ParityInputFF, ReplayRecordsTheInputsAsBytes) {
    phys::PedalInput in;
    in.steering = 0.5f;
    in.accelerator = 0.5f;
    in.brake = 1.0f;
    in.handbrake = 0.0f;
    const phys::PedalInput q = replayQuantize(in);
    EXPECT_FLOAT_EQ(q.steering, 63.0f * 0.007874016f); // ftol(63.5) = 63
    EXPECT_FLOAT_EQ(q.accelerator, 127.0f * 0.003921569f);
    EXPECT_FLOAT_EQ(q.brake, 255.0f * 0.003921569f);
    EXPECT_FLOAT_EQ(q.handbrake, 0.0f);
    in.steering = -1.0f;
    EXPECT_FLOAT_EQ(replayQuantize(in).steering, -127.0f * 0.007874016f);
}

TEST(ParityInputFF, CaptureReaderFollowsPollStates) {
    CaptureReader r;
    InputFrame f;
    f.mouseButtons = 1; // the click that started the capture
    r.begin(f);
    EXPECT_EQ(r.poll(f, Controller::Keyboard).kind, Captured::Kind::None); // waits for the release
    f.mouseButtons = 0;
    EXPECT_EQ(r.poll(f, Controller::Keyboard).kind, Captured::Kind::None);
    // Two keys at once are not taken; one is.
    f.keysPressed = {Key::A, Key::B};
    EXPECT_EQ(r.poll(f, Controller::Keyboard).kind, Captured::Kind::None);
    f.keysPressed = {Key::G};
    const Captured k = r.poll(f, Controller::Keyboard);
    EXPECT_EQ(k.kind, Captured::Kind::Key);
    EXPECT_EQ(k.value, static_cast<int>(Key::G));
    // The right mouse button alone; both together are not taken.
    f.keysPressed.clear();
    f.mouseButtons = 3;
    EXPECT_EQ(r.poll(f, Controller::Keyboard).kind, Captured::Kind::None);
    f.mouseButtons = 2;
    const Captured m = r.poll(f, Controller::Keyboard);
    EXPECT_EQ(m.kind, Captured::Kind::Mouse);
    EXPECT_EQ(m.value, 2);
    // Joystick controls only for the joystick types: the lowest button,
    // then an axis 0.125 from where it rested.
    InputFrame j = joyFrame(0.1f, 0.0f, 0b1100u);
    CaptureReader rj;
    rj.begin(joyFrame(0.1f, 0.0f));
    EXPECT_EQ(rj.poll(j, Controller::Keyboard).kind, Captured::Kind::None);
    const Captured b = rj.poll(j, Controller::Joystick);
    EXPECT_EQ(b.kind, Captured::Kind::JoyButton);
    EXPECT_EQ(b.value, 3);
    j.joy.buttons = 0;
    j.joy.x = 0.2f;
    EXPECT_EQ(rj.poll(j, Controller::Wheel).kind, Captured::Kind::None);
    j.joy.y = -0.5f;
    const Captured a = rj.poll(j, Controller::Wheel);
    EXPECT_EQ(a.kind, Captured::Kind::JoyAxis);
    EXPECT_EQ(a.value, component::kJoyYUp);
}
