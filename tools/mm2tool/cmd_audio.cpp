// Audio commands: sounds, play.
#include "Command.h"
#include "Common.h"

#include "audio/AudioDevice.h"
#include "audio/SoundBank.h"
#include "vfs/GameSource.h"

#include <SDL3/SDL.h>

#include <print>

namespace mm2::tool {
namespace {

int cmdPlay(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto source = vfs::probeGameSource(args[0]);
    vfs::Vfs v;
    std::string err;
    if (!source || !vfs::mountGameSource(v, *source, &err)) {
        std::println(stderr, "error: not a game source: {}", err);
        return 1;
    }
    audio::SoundBank bank(v);
    auto sound = bank.get(args[1]);
    if (!sound)
        return 1;
    std::println("{} ({} Hz, {} ch, {:.2f} s)", bank.resolve(args[1]), sound->sampleRate, sound->channels,
                 sound->seconds());
    auto mixer = std::make_shared<audio::Mixer>();
    audio::AudioDevice device;
    if (!device.open(mixer, &err)) {
        std::println(stderr, "error: audio device: {}", err);
        return 1;
    }
    const auto voice = mixer->play(sound, {});
    while (mixer->isPlaying(voice))
        SDL_Delay(20);
    SDL_Delay(100);
    return 0;
}

const Registrar r1({"play", "<game-source> <sound-name>", "play a game sound by name (e.g. VWIDLE)", &cmdPlay});

} // namespace
} // namespace mm2::tool
