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
    // Options > Audio: the volumes, with the toggles muting their buses
    // (sound FX: effects and engines; commentary: voices; music; city sounds:
    // the ambient sounds and the city's ambience segment).
    mixer->setMasterVolume(settings.masterVolume);
    mixer->setBusVolume(audio::Bus::Effects, settings.soundEffects ? settings.effectsVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Engine, settings.soundEffects ? settings.engineVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Ambient, settings.citySounds ? settings.ambientVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Voice, settings.commentary ? settings.voiceVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Music, settings.music ? settings.musicVolume : 0.0f);
    mixer->setBalance(settings.balance);
}

audio::MusicPlayer* Context::music() {
    if (!m_music && game && mixer) {
        m_music = std::make_unique<audio::MusicPlayer>(game->vfs, mixer->sampleRate());
        if (!m_music->ok()) {
            log::warn("music: soundtrack unavailable");
        } else {
            m_musicStream = mixer->addStream(m_music->musicStream(), audio::Bus::Music);
            // The city ambience segment follows the CITY SOUNDS option, not MUSIC.
            m_ambienceStream = mixer->addStream(m_music->ambienceStream(), audio::Bus::Ambient);
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
