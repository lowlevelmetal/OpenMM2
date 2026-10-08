#pragma once

#include "audio/Wav.h"
#include "vfs/Vfs.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mm2::audio {

// Finds and caches game sounds by the base names used in the data files
// ("VWIDLE" -> aud/aud22/engines/vwidle.22k.wav or aud/aud11/vwidle.11k.wav).
// The retail data ships most effects at 22 kHz and 11 kHz. MM2 always loads
// the 22 kHz files (InitAudioManager sets the default sub-path aud22 and
// extension .22K) and the 11 kHz ones only for speech (subpath aud11); its
// SOUND QUALITY option only stores a channel count that
// AudManager::SetNumChannels ignores. Quality::Low (11 kHz first) is an
// OpenMM2 choice the game does not use.
class SoundBank {
public:
    enum class Quality { Low, High }; // Low prefers 11 kHz files, High 22 kHz

    explicit SoundBank(const vfs::Vfs& vfs);

    void setQuality(Quality q);
    Quality quality() const { return m_quality; }

    // Returns the decoded sound or nullptr (logged once per missing name).
    std::shared_ptr<const SoundBuffer> get(std::string_view name);
    // Path of the file that get(name) would load, or empty.
    std::string resolve(std::string_view name) const;
    void clear() { m_cache.clear(); }
    std::size_t indexedCount() const { return m_index.size(); }

private:
    struct Variants {
        std::string k11, k22, other; // virtual paths
    };
    const vfs::Vfs& m_vfs;
    Quality m_quality = Quality::High;
    std::unordered_map<std::string, Variants> m_index;
    std::unordered_map<std::string, std::shared_ptr<const SoundBuffer>> m_cache;
    std::unordered_map<std::string, bool> m_reportedMissing;
};

} // namespace mm2::audio
