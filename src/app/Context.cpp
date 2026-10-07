#include "app/Context.h"

#include "core/Log.h"

namespace mm2::app {

void Context::saveSettings() {
    display.save(settings.ini);
    if (!settings.save(settingsPath))
        log::warn("settings: cannot write {}", settingsPath.string());
}

void Context::applyAudioSettings() {
    if (!mixer)
        return;
    mixer->setMasterVolume(settings.masterVolume);
    mixer->setBusVolume(audio::Bus::Effects, settings.effectsVolume);
    mixer->setBusVolume(audio::Bus::Engine, settings.engineVolume);
    mixer->setBusVolume(audio::Bus::Ambient, settings.ambientVolume);
    mixer->setBusVolume(audio::Bus::Voice, settings.voiceVolume);
    mixer->setBusVolume(audio::Bus::Music, settings.musicVolume);
    // Options > Audio "Balance" slider: 0 (left) .. 0.5 (centre) .. 1 (right).
    mixer->setBalance(static_cast<float>(settings.ini.getDouble("Audio", "Balance", 0.5)) * 2.0f - 1.0f);
}

audio::MusicPlayer* Context::music() {
    if (!m_music && game && mixer) {
        m_music = std::make_unique<audio::MusicPlayer>(game->vfs, mixer->sampleRate());
        if (!m_music->ok()) {
            log::warn("music: soundtrack unavailable");
        } else {
            m_musicStream = mixer->addStream(m_music->musicStream(), audio::Bus::Music);
            m_ambienceStream = mixer->addStream(m_music->ambienceStream(), audio::Bus::Music);
        }
    }
    return m_music.get();
}

void Context::shutdownMusic() {
    if (mixer) {
        mixer->removeStream(m_musicStream);
        mixer->removeStream(m_ambienceStream);
    }
    m_musicStream = m_ambienceStream = 0;
    m_music.reset();
}

std::unique_ptr<GameData> loadGameData(const vfs::GameSource& source, std::string* error) {
    auto data = std::make_unique<GameData>();
    data->source = source;
    if (!vfs::mountGameSource(data->vfs, source, error))
        return nullptr;
    data->catalog = game::Catalog::load(data->vfs);
    data->strings = game::Strings::load(source);
    return data;
}

} // namespace mm2::app
