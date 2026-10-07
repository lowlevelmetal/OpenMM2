#pragma once

#include "audio/Mixer.h"

#include <memory>
#include <string>

struct SDL_AudioStream;

namespace mm2::audio {

// Plays a Mixer through the default output device (SDL3). The mixer is
// pulled from SDL's audio thread.
class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Opens the default playback device at the mixer's sample rate.
    bool open(std::shared_ptr<Mixer> mixer, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_stream != nullptr; }

    // Pauses output (e.g. when the window loses focus, if configured).
    void setPaused(bool paused);
    std::string deviceName() const;

private:
    static void callback(void* user, SDL_AudioStream* stream, int additional, int total);

    std::shared_ptr<Mixer> m_mixer;
    SDL_AudioStream* m_stream = nullptr;
    bool m_initedSubsystem = false;
};

} // namespace mm2::audio
