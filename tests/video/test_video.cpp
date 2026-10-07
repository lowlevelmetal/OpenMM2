#include "TestData.h"
#include "vfs/GameSource.h"
#include "video/Avi.h"
#include "video/Indeo5.h"
#include "video/Movie.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <random>

using namespace mm2;

namespace {

// Builds a tiny AVI: one video stream (2 frames) and one 8-bit audio stream.
std::vector<std::byte> makeAvi() {
    std::vector<std::byte> d;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        d.insert(d.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    auto patch = [&](std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); };

    put("RIFF", 4);
    const std::size_t riffSize = d.size();
    u32(0);
    put("AVI ", 4);
    put("LIST", 4);
    const std::size_t hdrlSize = d.size();
    u32(0);
    put("hdrl", 4);
    put("avih", 4);
    u32(56);
    u32(66666); // us per frame
    u32(0);
    u32(0);
    u32(0x10);
    u32(2); // total frames
    u32(0);
    u32(2);
    u32(0);
    u32(320);
    u32(240);
    for (int i = 0; i < 4; ++i)
        u32(0);
    // video strl
    put("LIST", 4);
    u32(4 + 8 + 56 + 8 + 40);
    put("strl", 4);
    put("strh", 4);
    u32(56);
    put("vids", 4);
    put("IV50", 4);
    u32(0);
    u32(0);
    u32(0);
    u32(1);  // scale
    u32(15); // rate
    for (int i = 0; i < 7; ++i)
        u32(0);
    put("strf", 4);
    u32(40);
    u32(40);
    u32(320);
    u32(240);
    u16(1);
    u16(24);
    put("IV50", 4);
    for (int i = 0; i < 5; ++i)
        u32(0);
    // audio strl
    put("LIST", 4);
    u32(4 + 8 + 56 + 8 + 16);
    put("strl", 4);
    put("strh", 4);
    u32(56);
    put("auds", 4);
    for (int i = 0; i < 13; ++i)
        u32(0);
    put("strf", 4);
    u32(16);
    u16(1);
    u16(1);
    u32(22050);
    u32(22050);
    u16(1);
    u16(8);
    patch(hdrlSize, static_cast<std::uint32_t>(d.size() - hdrlSize - 4));
    // movi
    put("LIST", 4);
    const std::size_t moviSize = d.size();
    u32(0);
    put("movi", 4);
    put("01wb", 4);
    u32(3);
    const std::uint8_t pcm[3] = {128, 255, 0};
    put(pcm, 3);
    d.push_back(std::byte{0}); // pad
    put("00dc", 4);
    u32(4);
    u32(0x12345678);
    put("00dc", 4);
    u32(0); // dropped frame
    patch(moviSize, static_cast<std::uint32_t>(d.size() - moviSize - 4));
    patch(riffSize, static_cast<std::uint32_t>(d.size() - 8));
    return d;
}

} // namespace

TEST(Avi, ParsesStreamsAndChunks) {
    MemoryFile file(makeAvi());
    std::string err;
    auto avi = video::AviFile::parse(file, &err);
    ASSERT_TRUE(avi) << err;
    EXPECT_EQ(avi->info().width, 320);
    EXPECT_EQ(avi->info().height, 240);
    EXPECT_DOUBLE_EQ(avi->info().fps, 15.0);
    EXPECT_EQ(video::fourccString(avi->info().videoHandler), "IV50");
    ASSERT_EQ(avi->videoChunks().size(), 2u);
    EXPECT_EQ(avi->videoChunks()[0].size, 4u);
    EXPECT_EQ(avi->videoChunks()[1].size, 0u);
    auto audio = avi->readAudio(file, &err);
    ASSERT_TRUE(audio) << err;
    EXPECT_EQ(audio->sampleRate, 22050);
    ASSERT_EQ(audio->samples.size(), 3u);
    EXPECT_EQ(audio->samples[0], 0);
    EXPECT_EQ(audio->samples[1], 127 << 8);
    EXPECT_EQ(audio->samples[2], -128 << 8);
}

TEST(Avi, RejectsNonAvi) {
    MemoryFile file(std::vector<std::byte>(64, std::byte{0x41}));
    EXPECT_FALSE(video::AviFile::parse(file));
}

TEST(Indeo5, SurvivesGarbage) {
    video::Indeo5Decoder dec;
    video::YuvFrame frame;
    std::mt19937 rng(1234);
    for (int i = 0; i < 2000; ++i) {
        std::vector<std::byte> data(static_cast<std::size_t>(rng() % 3000));
        for (auto& b : data)
            b = static_cast<std::byte>(rng());
        // Picture start code + intra frame type so the GOP parser is exercised.
        if (data.size() > 2 && (i & 1))
            data[0] = std::byte{0x1F};
        (void)dec.decode(data, frame);
    }
    SUCCEED();
}

TEST(Indeo5, DecodesRetailIntroMovie) {
    MM2_REQUIRE_GAME_DATA();
    auto source = vfs::probeGameSource(std::getenv("OPENMM2_GAME_DATA"));
    ASSERT_TRUE(source);
    std::string err;
    auto file = vfs::openSourceFile(*source, "LOGOS.AVI", &err);
    if (!file)
        GTEST_SKIP() << "LOGOS.AVI not in this game source: " << err;
    auto movie = video::Movie::open(file, &err);
    ASSERT_TRUE(movie) << err;
    EXPECT_EQ(movie->width(), 320);
    EXPECT_EQ(movie->height(), 240);
    EXPECT_NEAR(movie->fps(), 15.0, 0.01);
    EXPECT_EQ(movie->frameCount(), static_cast<int>(movie->info().totalFrames));

    video::YuvFrame frame;
    int frames = 0, errors = 0;
    double lumaSum = 0;
    while (true) {
        std::string frameErr;
        if (!movie->decodeNext(frame, &frameErr))
            break;
        if (!frameErr.empty()) {
            ++errors;
            ADD_FAILURE() << frameErr;
        }
        ASSERT_EQ(frame.width, 320);
        ASSERT_EQ(frame.y.size(), 320u * 240u);
        for (auto v : frame.y)
            lumaSum += v;
        ++frames;
    }
    EXPECT_EQ(frames, movie->frameCount());
    EXPECT_EQ(errors, 0);
    EXPECT_GT(lumaSum / (frames * 320.0 * 240.0), 5.0); // not all black

    auto audio = movie->readAudio(&err);
    ASSERT_TRUE(audio) << err;
    EXPECT_EQ(audio->sampleRate, 22050);
    EXPECT_NEAR(audio->seconds(), frames / movie->fps(), 0.1);
}
