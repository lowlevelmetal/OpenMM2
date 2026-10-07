#include "audio/AudioDevice.h"

#include "core/Log.h"

#include <SDL3/SDL.h>

#include <vector>

namespace mm2::audio {

AudioDevice::~AudioDevice() { close(); }

bool AudioDevice::open(std::shared_ptr<Mixer> mixer, std::string* error) {
    close();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (error)
            *error = SDL_GetError();
        return false;
    }
    m_initedSubsystem = true;
    m_mixer = std::move(mixer);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, m_mixer->sampleRate()};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::callback, this);
    if (!m_stream) {
        if (error)
            *error = SDL_GetError();
        close();
        return false;
    }
    SDL_ResumeAudioStreamDevice(m_stream);
    log::info("audio: output '{}' at {} Hz", deviceName(), m_mixer->sampleRate());
    return true;
}

void AudioDevice::close() {
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    if (m_initedSubsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_initedSubsystem = false;
    }
    m_mixer.reset();
}

void AudioDevice::setPaused(bool paused) {
    if (!m_stream)
        return;
    if (paused)
        SDL_PauseAudioStreamDevice(m_stream);
    else
        SDL_ResumeAudioStreamDevice(m_stream);
}

std::string AudioDevice::deviceName() const {
    if (!m_stream)
        return {};
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(m_stream));
    return name ? name : "default";
}

void AudioDevice::callback(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<AudioDevice*>(user);
    if (additional <= 0 || !self->m_mixer)
        return;
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    // Audio thread: reuse a thread-local scratch buffer to avoid allocations.
    thread_local std::vector<float> buffer;
    buffer.resize(static_cast<std::size_t>(frames) * 2);
    self->m_mixer->mix(buffer.data(), frames);
    SDL_PutAudioStreamData(stream, buffer.data(), frames * static_cast<int>(sizeof(float) * 2));
}

} // namespace mm2::audio
