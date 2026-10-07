// Game sound effect commands: carsound.
#include "Command.h"
#include "Common.h"

#include "audio/AudioDevice.h"
#include "audio/game/CarAudio.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <print>

namespace mm2::tool {
namespace {

std::vector<std::byte> stereoWav(const std::vector<float>& samples, int rate) {
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    put("RIFF", 4);
    u32(36 + bytes);
    put("WAVE", 4);
    put("fmt ", 4);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 4));
    u16(4);
    u16(16);
    put("data", 4);
    u32(bytes);
    for (float f : samples) {
        const auto s = static_cast<std::int16_t>(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        put(&s, 2);
    }
    return out;
}

// Inputs over time for the demonstration: RPM sweep up and down, then a skid
// and a hard impact.
audio::game::CarAudioInputs scriptAt(double t, float maxRpm) {
    audio::game::CarAudioInputs in;
    for (auto& w : in.wheels)
        w.onGround = true;
    const double up = 4.0, down = 4.0;
    if (t < up)
        in.rpm = static_cast<float>(800.0 + (maxRpm - 800.0) * (t / up));
    else if (t < up + down)
        in.rpm = static_cast<float>(maxRpm - (maxRpm - 800.0) * ((t - up) / down));
    else
        in.rpm = 800.0f;
    in.throttle = t < up ? 1.0f : 0.0f;
    in.speed = t < up + down ? static_cast<float>(5.0 + 25.0 * std::sin(std::min(t, 8.0) / 8.0 * 3.14159)) : 15.0f;
    if (t >= 8.0 && t < 9.5)
        for (auto& w : in.wheels)
            w.slip = 0.9f;
    return in;
}

int cmdCarSound(std::span<char* const> args) {
    if (args.size() < 2) {
        std::println(stderr, "usage: carsound <game-source> <car> [--rpm-sweep] [--wav out.wav] [--play]");
        return 2;
    }
    std::string wavPath;
    bool play = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        const std::string_view a = args[i];
        if (a == "--wav" && i + 1 < args.size())
            wavPath = args[++i];
        else if (a == "--play")
            play = true;
    }
    auto source = vfs::probeGameSource(str::toPath(args[0]));
    vfs::Vfs v;
    std::string err;
    if (!source || !vfs::mountGameSource(v, *source, &err)) {
        std::println(stderr, "error: not a game source: {}", err);
        return 1;
    }
    audio::SoundBank bank(v);
    constexpr int kRate = 48000;
    auto mixer = std::make_shared<audio::Mixer>(kRate);
    audio::game::PlayerCarAudio car;
    if (!car.load(v, bank, *mixer, args[1], {}, &err)) {
        std::println(stderr, "error: {}", err);
        return 1;
    }
    float maxRpm = 0;
    for (const auto& s : car.definition().engine)
        maxRpm = std::max(maxRpm, std::min(s.fadeOutEndRpm, 12000.0f));
    maxRpm = std::clamp(maxRpm, 3000.0f, 12000.0f);
    std::println("{}: {} engine samples, horn {}, clutch {}, sweep 800..{:.0f} RPM", args[1],
                 car.definition().engine.size(), car.definition().horn, car.definition().clutch, maxRpm);

    constexpr int kBlock = 512;
    const double duration = 10.0;
    std::vector<float> out;
    std::vector<float> block(kBlock * 2);
    bool impacted = false;
    for (int frame = 0; frame < static_cast<int>(duration * kRate); frame += kBlock) {
        const double t = static_cast<double>(frame) / kRate;
        auto in = scriptAt(t, maxRpm);
        if (t >= 9.0 && !impacted) {
            in.impacts.push_back({25000.0f, 0, {}});
            impacted = true;
        }
        car.update(in, static_cast<float>(kBlock) / kRate);
        mixer->mix(block.data(), kBlock);
        out.insert(out.end(), block.begin(), block.end());
    }

    // Level report per second: peak, RMS (dBFS), clipped samples.
    int clipped = 0;
    for (int s = 0; s < static_cast<int>(duration); ++s) {
        float peak = 0;
        double sum = 0;
        const std::size_t a = static_cast<std::size_t>(s) * kRate * 2, b = std::min(out.size(), a + kRate * 2);
        for (std::size_t i = a; i < b; ++i) {
            peak = std::max(peak, std::abs(out[i]));
            sum += static_cast<double>(out[i]) * out[i];
            clipped += std::abs(out[i]) > 1.0f ? 1 : 0;
        }
        const double rms = std::sqrt(sum / static_cast<double>(std::max<std::size_t>(b - a, 1)));
        std::println("  {:2}s  peak {:.3f}  rms {:6.1f} dBFS", s, peak, rms > 0 ? 20.0 * std::log10(rms) : -120.0);
    }
    std::println("clipped samples: {}", clipped);

    if (!wavPath.empty()) {
        if (!file::writeAtomic(str::toPath(wavPath), stereoWav(out, kRate))) {
            std::println(stderr, "error: cannot write {}", wavPath);
            return 1;
        }
        std::println("wrote {}", wavPath);
    }
    if (play) {
        // Short live check through the real device: the first 5 seconds of the sweep.
        car.stop();
        audio::AudioDevice device;
        if (!device.open(mixer, &err)) {
            std::println(stderr, "error: audio device: {}", err);
            return 1;
        }
        const auto start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 5000) {
            const double t = static_cast<double>(SDL_GetTicks() - start) / 1000.0;
            car.update(scriptAt(t, maxRpm), 0.02f);
            SDL_Delay(20);
        }
        car.stop();
    }
    return clipped > 0 ? 3 : 0;
}

const Registrar r1({"carsound", "<game-source> <car> [--rpm-sweep] [--wav out.wav] [--play]",
                    "render a car's engine sweep, skid and impact through the game audio mixer", &cmdCarSound});

} // namespace
} // namespace mm2::tool
