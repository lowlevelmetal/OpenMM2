#include "TestData.h"
#include "audio/Mixer.h"
#include "audio/SoundBank.h"
#include "audio/Wav.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace mm2;
using namespace mm2::audio;

namespace {

std::vector<std::byte> makeWav(int rate, int channels, const std::vector<std::int16_t>& samples) {
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    put("RIFF", 4);
    u32(36 + dataBytes);
    put("WAVE", 4);
    put("fmt ", 4);
    u32(16);
    u16(1);
    u16(static_cast<std::uint16_t>(channels));
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * channels * 2));
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    put("data", 4);
    u32(dataBytes);
    put(samples.data(), dataBytes);
    return out;
}

std::shared_ptr<SoundBuffer> constant(int rate, int frames, std::int16_t value) {
    auto s = std::make_shared<SoundBuffer>();
    s->sampleRate = rate;
    s->channels = 1;
    s->samples.assign(static_cast<std::size_t>(frames), value);
    return s;
}

} // namespace

TEST(Wav, DecodesPcm16) {
    auto bytes = makeWav(22050, 2, {1, -1, 1000, -1000});
    std::string err;
    auto s = decodeWav(bytes, &err);
    ASSERT_TRUE(s) << err;
    EXPECT_EQ(s->sampleRate, 22050);
    EXPECT_EQ(s->channels, 2);
    EXPECT_EQ(s->frames(), 2u);
    EXPECT_EQ(s->samples[3], -1000);
}

TEST(Wav, RejectsGarbage) {
    std::vector<std::byte> junk(64, std::byte{0x41});
    EXPECT_FALSE(decodeWav(junk));
    auto truncated = makeWav(11025, 1, {1, 2, 3});
    truncated.resize(20);
    EXPECT_FALSE(decodeWav(truncated));
}

TEST(Mixer, OneShotFinishesAndLoopContinues) {
    Mixer m(1000);
    auto s = constant(1000, 100, 16384);
    const VoiceHandle once = m.play(s, {});
    VoiceParams loopParams;
    loopParams.loop = true;
    const VoiceHandle loop = m.play(s, loopParams);
    std::vector<float> out(2 * 150);
    m.mix(out.data(), 150);
    EXPECT_FALSE(m.isPlaying(once));
    EXPECT_TRUE(m.isPlaying(loop));
    // Both voices contribute 0.5 at full volume while the one-shot plays.
    EXPECT_NEAR(out[0], 1.0f, 1e-3f);
    EXPECT_NEAR(out[2 * 120], 0.5f, 1e-3f);
}

TEST(Mixer, PanAndBusVolume) {
    Mixer m(1000);
    auto s = constant(1000, 1000, 16384);
    VoiceParams p;
    p.pan = 1.0f;
    p.bus = Bus::Engine;
    m.setBusVolume(Bus::Engine, 0.5f);
    m.play(s, p);
    std::vector<float> out(2 * 10);
    m.mix(out.data(), 10);
    EXPECT_NEAR(out[0], 0.0f, 1e-4f);  // left silenced
    EXPECT_NEAR(out[1], 0.25f, 1e-3f); // right: 0.5 sample * 0.5 bus
}

TEST(Mixer, DistanceRolloffFollowsDirectSound3D) {
    Mixer m(1000);
    m.setDopplerFactor(0.0f);
    auto s = constant(1000, 1000, 32767);
    VoiceParams p;
    p.spatial = Emitter3D{{0, 0, -20}, {}, 5.0f, 1000.0f}; // straight ahead, 4x min distance
    m.play(s, p);
    std::vector<float> out(2 * 10);
    m.mix(out.data(), 10);
    // gain = min / (min + rolloff * (d - min)) = 5 / 20
    EXPECT_NEAR(out[0], 0.25f, 1e-3f);
    EXPECT_NEAR(out[1], 0.25f, 1e-3f);
}

TEST(Mixer, StealsOldestWhenFull) {
    Mixer m(1000, 2);
    auto s = constant(1000, 1000, 1);
    const auto a = m.play(s, {});
    const auto b = m.play(s, {});
    const auto c = m.play(s, {});
    EXPECT_FALSE(m.isPlaying(a));
    EXPECT_TRUE(m.isPlaying(b));
    EXPECT_TRUE(m.isPlaying(c));
    EXPECT_EQ(m.activeVoices(), 2);
}

TEST(SoundBank, DecodesEveryRetailWav) {
    MM2_REQUIRE_GAME_DATA();
    int count = 0;
    for (const auto& e : test::gameData()->listFiles()) {
        if (!e.path.ends_with(".wav"))
            continue;
        auto bytes = test::gameData()->readAll(e.path);
        ASSERT_TRUE(bytes);
        std::string err;
        auto s = decodeWav(*bytes, &err);
        EXPECT_TRUE(s) << e.path << ": " << err;
        ++count;
    }
    EXPECT_GT(count, 2000);

    SoundBank bank(*test::gameData());
    EXPECT_TRUE(bank.get("VWIDLE"));
    bank.setQuality(SoundBank::Quality::Low);
    EXPECT_NE(bank.resolve("vwdrive").find("11k"), std::string::npos);
}

namespace {
struct ConstantStream final : StreamSource {
    float value;
    explicit ConstantStream(float v) : value(v) {}
    void render(float* stereo, int frames) override { std::fill(stereo, stereo + 2 * frames, value); }
};
} // namespace

TEST(Mixer, StreamsMixOnTheirBus) {
    Mixer m(1000);
    const int id = m.addStream(std::make_shared<ConstantStream>(0.5f), Bus::Music, 0.5f);
    m.setBusVolume(Bus::Music, 0.5f);
    std::vector<float> out(2 * 8);
    m.mix(out.data(), 8);
    EXPECT_NEAR(out[0], 0.125f, 1e-6f);
    m.removeStream(id);
    m.mix(out.data(), 8);
    EXPECT_EQ(out[0], 0.0f);
}
