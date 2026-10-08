// SDL3 playback of Midtown Madness 2's force-feedback effects (DirectInput
// in the original; the effect parameters are those of mmFrictionFF,
// mmCollideFF, mmRoadFF and mmSpringFF ::Init / ::Assign, build 3393).

#include "platform/ForceFeedback.h"

#include "core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>

namespace mm2::platform {
namespace {

// DirectInput's 0 .. 10000 to SDL's 0 .. 32767.
Sint16 level(int v) { return static_cast<Sint16>(std::clamp(v, 0, 10000) * 32767 / 10000); }

constexpr std::size_t slot(FFEffect e) { return static_cast<std::size_t>(e); }

// A force-feedback device with SDL haptic effects.
class HapticDevice final : public FFDevice {
public:
    explicit HapticDevice(SDL_Haptic* h) : m_haptic(h) {
        const Uint32 features = SDL_GetHapticFeatures(h);
        // mmJoystick::DisableAutoCenter (DIPROP_AUTOCENTER off).
        if (features & SDL_HAPTIC_AUTOCENTER)
            SDL_SetHapticAutocenter(h, 0);
        // inputEnumEffectTypeProc takes the first periodic type the driver
        // lists (DirectInput lists the square wave before the sine).
        const Uint16 wave = (features & SDL_HAPTIC_SQUARE) ? SDL_HAPTIC_SQUARE
                            : (features & SDL_HAPTIC_SINE) ? SDL_HAPTIC_SINE
                                                           : 0;
        m_ids.fill(-1);
        // mmJoystick::InputInitEffect: road, spring, collision, friction.
        if (wave) {
            m_effects[slot(FFEffect::Road)] = roadEffect(wave, 2000, 5000000);
            m_effects[slot(FFEffect::Collision)] = collisionEffect(wave, 10000, 9000);
            create(FFEffect::Road);
            create(FFEffect::Collision);
        }
        if (features & SDL_HAPTIC_SPRING) {
            m_effects[slot(FFEffect::Spring)] = conditionEffect(SDL_HAPTIC_SPRING, 0);
            create(FFEffect::Spring);
        }
        if (features & SDL_HAPTIC_FRICTION) {
            m_effects[slot(FFEffect::Friction)] = conditionEffect(SDL_HAPTIC_FRICTION, 0);
            create(FFEffect::Friction);
        }
    }
    ~HapticDevice() override {
        SDL_StopHapticEffects(m_haptic);
        for (int id : m_ids)
            if (id >= 0)
                SDL_DestroyHapticEffect(m_haptic, id);
        SDL_CloseHaptic(m_haptic);
    }

    Kind kind() const override { return Kind::Haptic; }
    bool has(FFEffect e) const override { return m_ids[slot(e)] >= 0; }
    bool play(FFEffect e) override { return has(e) && SDL_RunHapticEffect(m_haptic, m_ids[slot(e)], 1); }
    bool stop(FFEffect e) override { return has(e) && SDL_StopHapticEffect(m_haptic, m_ids[slot(e)]); }
    void stopAll() override { SDL_StopHapticEffects(m_haptic); }

    bool setCondition(FFEffect e, int coefficient) override {
        // mmFrictionFF::Assign / mmSpringFF::Assign: both coefficients,
        // saturation 10000 both ways, no dead band, centred.
        if (!has(e))
            return false;
        SDL_HapticCondition& c = m_effects[slot(e)].condition;
        c.right_coeff[0] = c.left_coeff[0] = level(coefficient);
        c.right_sat[0] = c.left_sat[0] = 0xFFFF;
        return update(e);
    }
    bool setRoad(int magnitude, int periodMicroseconds) override {
        // mmRoadFF::Assign: the magnitude and the period (DIP_TYPESPECIFIC).
        if (!has(FFEffect::Road))
            return false;
        SDL_HapticPeriodic& p = m_effects[slot(FFEffect::Road)].periodic;
        p.magnitude = level(magnitude);
        p.period = periodMs(periodMicroseconds);
        return update(FFEffect::Road);
    }
    bool setCollision(int gain, int direction) override {
        // mmCollideFF::Assign: the gain and the direction (the 0.1 s length
        // stays the one Init gave). SDL has no per-effect gain, so it scales
        // the magnitude, as DirectInput's gain does.
        if (!has(FFEffect::Collision))
            return false;
        SDL_HapticPeriodic& p = m_effects[slot(FFEffect::Collision)].periodic;
        p.magnitude = level(gain);
        p.direction.type = SDL_HAPTIC_POLAR;
        p.direction.dir[0] = std::clamp(direction, 0, 36000);
        return update(FFEffect::Collision);
    }
    void update(float) override {}

private:
    static Uint16 periodMs(int us) { return static_cast<Uint16>(std::clamp((us + 500) / 1000, 1, 65535)); }

    static SDL_HapticEffect conditionEffect(Uint16 type, int coefficient) {
        // mmFrictionFF::Init / mmSpringFF::Init: one axis (X), infinite.
        SDL_HapticEffect e{};
        e.condition.type = type;
        e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.condition.length = SDL_HAPTIC_INFINITY;
        e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
        e.condition.right_coeff[0] = e.condition.left_coeff[0] = level(coefficient);
        return e;
    }
    static SDL_HapticEffect roadEffect(Uint16 wave, int magnitude, int periodUs) {
        // mmRoadFF::Init: one axis (X), infinite, magnitude 2000, period 5 s.
        SDL_HapticEffect e{};
        e.periodic.type = wave;
        e.periodic.direction.type = SDL_HAPTIC_STEERING_AXIS;
        e.periodic.length = SDL_HAPTIC_INFINITY;
        e.periodic.magnitude = level(magnitude);
        e.periodic.period = periodMs(periodUs);
        return e;
    }
    static SDL_HapticEffect collisionEffect(Uint16 wave, int gain, int direction) {
        // mmCollideFF::Init: X and Y in polar form (90 degrees), 0.1 s
        // (InputDuration x 10^6), period 2 s (10^6 / mmInput +0x168, 0.5),
        // full magnitude; the envelope's attack level 10000 over no time
        // changes nothing.
        SDL_HapticEffect e{};
        e.periodic.type = wave;
        e.periodic.direction.type = SDL_HAPTIC_POLAR;
        e.periodic.direction.dir[0] = direction;
        e.periodic.length = 100;
        e.periodic.period = 2000;
        e.periodic.magnitude = level(gain);
        return e;
    }

    void create(FFEffect e) {
        const int id = SDL_CreateHapticEffect(m_haptic, &m_effects[slot(e)]);
        if (id < 0)
            log::info("input: force feedback effect {} unavailable: {}", static_cast<int>(e), SDL_GetError());
        m_ids[slot(e)] = id;
    }
    bool update(FFEffect e) { return SDL_UpdateHapticEffect(m_haptic, m_ids[slot(e)], &m_effects[slot(e)]); }

    SDL_Haptic* m_haptic;
    std::array<SDL_HapticEffect, 4> m_effects{};
    std::array<int, 4> m_ids{};
};

// A device with only rumble motors (OpenMM2: inferred stand-ins). The road
// wave turns the large motor at its magnitude while it plays; a collision is
// a 0.1 s burst of both motors at its gain. Friction and the centring
// spring have no rumble equivalent.
class RumbleDevice final : public FFDevice {
public:
    explicit RumbleDevice(SDL_Joystick* j) : m_joystick(j) {}
    ~RumbleDevice() override { SDL_RumbleJoystick(m_joystick, 0, 0, 0); }

    Kind kind() const override { return Kind::Rumble; }
    bool has(FFEffect e) const override { return e == FFEffect::Road || e == FFEffect::Collision; }
    bool play(FFEffect e) override {
        if (e == FFEffect::Road)
            m_roadPlaying = true;
        else if (e == FFEffect::Collision)
            m_burst = 0.1f;
        else
            return false;
        refresh();
        return true;
    }
    bool stop(FFEffect e) override {
        if (e == FFEffect::Road)
            m_roadPlaying = false;
        else if (e == FFEffect::Collision)
            m_burst = 0.0f;
        else
            return false;
        refresh();
        return true;
    }
    void stopAll() override {
        m_roadPlaying = false;
        m_burst = 0.0f;
        refresh();
    }
    bool setCondition(FFEffect, int) override { return false; }
    bool setRoad(int magnitude, int) override {
        m_road = std::clamp(magnitude, 0, 10000);
        return true;
    }
    bool setCollision(int gain, int) override {
        m_gain = std::clamp(gain, 0, 10000);
        return true;
    }
    void update(float dt) override {
        if (m_burst > 0.0f)
            m_burst = std::max(0.0f, m_burst - dt);
        refresh();
    }

private:
    void refresh() {
        const int road = m_roadPlaying ? m_road : 0;
        const int burst = m_burst > 0.0f ? m_gain : 0;
        const auto motor = [](int v) { return static_cast<Uint16>(v * 65535 / 10000); };
        const Uint16 low = motor(std::max(road, burst)), high = motor(burst);
        if (low == 0 && high == 0) {
            if (m_active)
                SDL_RumbleJoystick(m_joystick, 0, 0, 0);
            m_active = false;
            return;
        }
        SDL_RumbleJoystick(m_joystick, low, high, 200);
        m_active = true;
    }

    SDL_Joystick* m_joystick;
    bool m_roadPlaying = false;
    bool m_active = false;
    int m_road = 0, m_gain = 0;
    float m_burst = 0.0f;
};

} // namespace

std::unique_ptr<FFDevice> openForceFeedback(void* sdlJoystick) {
    auto* j = static_cast<SDL_Joystick*>(sdlJoystick);
    if (!j)
        return nullptr;
    if (SDL_IsJoystickHaptic(j)) {
        if (SDL_Haptic* h = SDL_OpenHapticFromJoystick(j)) {
            constexpr Uint32 kUsable = SDL_HAPTIC_SQUARE | SDL_HAPTIC_SINE | SDL_HAPTIC_SPRING | SDL_HAPTIC_FRICTION;
            if (SDL_GetHapticFeatures(h) & kUsable) {
                log::info("input: force feedback device found");
                return std::make_unique<HapticDevice>(h);
            }
            SDL_CloseHaptic(h);
        }
    }
    if (SDL_GetBooleanProperty(SDL_GetJoystickProperties(j), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false))
        return std::make_unique<RumbleDevice>(j);
    return nullptr;
}

} // namespace mm2::platform
