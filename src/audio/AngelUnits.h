#pragma once

// MM2's volume and pan units and how its audio layer hands them to
// DirectSound (audSound::SetVolume / SetPan, audObject::SetVolume,
// AudManager::AssignWaveVolume, DMusicWaveBuffer::SetVolume), converted to the
// mixer's linear gains.

namespace mm2::audio {

// Angel volume (AudSoundBase::SetVolume) to linear gain. MM2's
// audSound::SetVolume passes (v - 1) * 10000 to IDirectSoundBuffer::SetVolume,
// so v is linear in decibels: 1 = 0 dB, 0.9 = -10 dB, 0.25 = -75 dB,
// 0 = -100 dB.
float ageVolumeToGain(float v);

// Angel pan (AudSoundBase::SetPan, -1 left .. 1 right) to the mixer's pan.
// audSound::SetPan passes pan * 10000 to IDirectSoundBuffer::SetPan, which
// attenuates the opposite channel by |pan| * 100 dB and leaves the near one
// at full level; the mixer attenuates the opposite channel linearly by |pan|.
// A DirectSound pan of 0.2, the most MM2's 3D sounds use, is -20 dB on the
// far channel.
float agePanToMixer(float pan);

// AudManager::AssignWaveVolume: the SOUND FX slider s (0..1) becomes the
// master volume log(200 s) / log(200) (0 for s = 0), which audObject::SetVolume
// multiplies into every Angel volume before clamping it to 0..1. A half-way
// slider is a master of 0.869: -13 dB on a sound at volume 1.
// DMusicWaveBuffer::SetVolume maps the MUSIC slider the same way and sets the
// DirectMusic buffer to that Angel volume.
float ageMasterVolume(float slider);

} // namespace mm2::audio
