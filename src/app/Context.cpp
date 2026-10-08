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
    // (sound FX: effects, engines and voices, the city's wave ambient sounds
    // included; commentary: voices; music; city sounds: the city's DirectMusic
    // ambience segment, which DMusicWaveBuffer::SetVolume plays at the MUSIC
    // volume).
    // AudioOptions::SetAudioState: turning SOUND FX off sets the volume of
    // every AudSoundBase sound to zero (audManager::SetVolAllSounds on the
    // sound class that SOUND FX VOLUME scales through
    // AudManager::AssignWaveVolume). The commentary's AudSpeech streams are
    // AudSoundBase sounds, so commentary falls silent too.
    mixer->setMasterVolume(settings.masterVolume);
    mixer->setBusVolume(audio::Bus::Effects, settings.soundEffects ? settings.effectsVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Engine, settings.soundEffects ? settings.engineVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Ambient, settings.citySounds ? settings.musicVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Voice,
                        settings.soundEffects && settings.commentary ? settings.voiceVolume : 0.0f);
    mixer->setBusVolume(audio::Bus::Music, settings.music ? settings.musicVolume : 0.0f);
    mixer->setBalance(settings.balance);
    // AudioOptions::SetStereoFX: mono centres every voice; surround only adds
    // an EAX/3D flag, so it plays as stereo.
    mixer->setStereo(settings.stereoFx != 0);
}

audio::MusicPlayer* Context::music() {
    if (!m_music && game && mixer) {
        m_music = std::make_unique<audio::MusicPlayer>(game->vfs, mixer->sampleRate());
        if (!m_music->ok()) {
            log::warn("music: soundtrack unavailable");
        } else {
            m_musicStream = mixer->addStream(m_music->musicStream(), audio::Bus::Music);
            // The city ambience segment: CITY SOUNDS turns it on, the MUSIC
            // volume sets its level (applyAudioSettings).
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
