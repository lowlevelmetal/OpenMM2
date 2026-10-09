// Parity checks of the order in which MM2 updates its audio (MM2Recomp,
// build 3393): AudManager::Update runs twice a frame, once from GameLoop and
// once as asRoot's first node. See docs/parity/round3/order.md.
#include "audio/AngelRandom.h"
#include "audio/Mixer.h"
#include "audio/SoundBank.h"
#include "audio/game/AudioManager.h"
#include "audio/game/Voices.h"
#include "core/File.h"
#include "vfs/DirectoryFs.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace mm2;
using namespace mm2::audio;
using namespace mm2::audio::game;

namespace {

std::vector<std::byte> wav(int frames) {
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    const std::uint32_t rate = 22050, data = static_cast<std::uint32_t>(frames) * 2, size = 36 + data;
    const std::uint32_t fmt = 16, bytes = rate * 2;
    const std::uint16_t pcm = 1, mono = 1, align = 2, bits = 16;
    const std::int16_t level = 8000;
    put("RIFF", 4), put(&size, 4), put("WAVE", 4), put("fmt ", 4), put(&fmt, 4), put(&pcm, 2), put(&mono, 2);
    put(&rate, 4), put(&bytes, 4), put(&align, 2), put(&bits, 2), put("data", 4), put(&data, 4);
    for (int i = 0; i < frames; ++i)
        put(&level, 2);
    return out;
}

// A throwaway game-data tree with one-second 22 kHz sounds and text files.
struct OrderData {
    std::filesystem::path root;
    vfs::Vfs vfs;
    OrderData(std::initializer_list<std::pair<const char*, std::string>> files,
              std::initializer_list<const char*> sounds) {
        root = std::filesystem::temp_directory_path() /
               ("openmm2_audio_order_" + std::to_string(std::rand()) +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        for (const auto& [path, text] : files)
            file::writeAtomic(root / path, text);
        for (const char* s : sounds) {
            file::writeAtomic(root / "aud/aud22" / (std::string(s) + ".22k.wav"),
                              std::span<const std::byte>(wav(22050)));
            file::writeAtomic(root / "aud/aud11" / (std::string(s) + ".11k.wav"),
                              std::span<const std::byte>(wav(22050)));
        }
        vfs.mount(std::make_shared<vfs::DirectoryFs>(root));
    }
    ~OrderData() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

// GameLoop calls AudManager::Update, then asRoot::Update calls it again as
// its first node: AudManagerBase::UpdatePaused's two stops (its counter
// limit of 1) both come in the first paused frame.
TEST(AudioOrderParity, BothPausedStopsComeInTheFirstPausedFrame) {
    Mixer mixer(48000);
    AudioManager manager;
    EXPECT_FALSE(manager.updateFrame(false, mixer, nullptr, 0.03f));
    EXPECT_EQ(manager.pausedUpdates(), 0);
    EXPECT_TRUE(manager.updateFrame(true, mixer, nullptr, 0.03f));
    EXPECT_EQ(manager.pausedUpdates(), 2);
    EXPECT_FALSE(manager.updateFrame(true, mixer, nullptr, 0.03f));
    EXPECT_EQ(manager.pausedUpdates(), 2);
    // Running again: the counter goes back to 0.
    EXPECT_FALSE(manager.updateFrame(false, mixer, nullptr, 0.03f));
    EXPECT_EQ(manager.pausedUpdates(), 0);
}

// With mmGame::Update's own mmSpeechContainer::Update, the announcer's
// queue counts three times a frame while the game runs: the pre-race line,
// queued for 1.5 s, starts after 0.5 s.
TEST(AudioOrderParity, AnnouncerQueueCountsThreeTimesAFrameWhileRunning) {
    const char* blitz = "Name prefix/type header,end sufix value,sufix add value\nPRERACE header,,\nPRE,2,0\n";
    OrderData data({{"aud/spchdata/london.csv", "Num announcers\n1\nprefix\nAL\n"}, {"aud/spchdata/al1/blitz.csv", blitz}},
                   {"al1pre01", "al1pre02"});
    SoundBank bank(data.vfs);
    Mixer mixer(48000);
    Announcer a;
    ASSERT_TRUE(a.load(data.vfs, bank, mixer, "london"));
    a.beginRace(AnnouncerMode::Blitz, {}, 1, 4);
    std::string line;
    for (int i = 0; i < 40 && line.empty(); ++i) {
        setRandomizeSeedSource([i] { return 1000 + i; });
        line = a.playPreRace();
    }
    setRandomizeSeedSource({});
    ASSERT_EQ(line, "al1pre"); // queued for 1.5 s
    AudioManager manager;
    auto frame = [&] {
        manager.updateFrame(false, mixer, &a, 0.125f);
        a.update(0.125f); // mmGame::Update
    };
    for (int i = 0; i < 3; ++i) {
        frame();
        EXPECT_EQ(mixer.activeVoices(), 0) << i; // up to 1.125 s counted
    }
    frame();
    EXPECT_EQ(mixer.activeVoices(), 1); // 1.5 s counted in 0.5 s: the line starts
    a.stop();
}
