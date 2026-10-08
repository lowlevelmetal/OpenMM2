// MM2's volume and pan units; see AngelUnits.h.
#include "audio/AngelUnits.h"

#include <algorithm>
#include <cmath>

namespace mm2::audio {

float ageVolumeToGain(float v) {
    if (!(v > 0.0f))
        return 0.0f;
    if (v >= 1.0f)
        return 1.0f;
    // (v - 1) * 10000 hundredths of a decibel -> amplitude.
    return std::pow(10.0f, (v - 1.0f) * 5.0f);
}

float agePanToMixer(float pan) {
    // pan * 10000 hundredths of a decibel on the far channel -> its gain.
    const float a = std::min(std::abs(pan), 1.0f);
    const float far = std::pow(10.0f, -a * 5.0f);
    return pan < 0.0f ? -(1.0f - far) : 1.0f - far;
}

float ageMasterVolume(float slider) {
    // AudManager::Log(200, slider * 200): log10(slider * 200) / log10(200).
    if (slider == 0.0f)
        return 0.0f;
    const float scaled = slider * 200.0f;
    return static_cast<float>(std::log10(static_cast<double>(scaled)) / std::log10(200.0));
}

} // namespace mm2::audio
