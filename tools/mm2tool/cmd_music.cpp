// Music commands: musicinfo, music.
#include "Command.h"
#include "Common.h"

#include "audio/AudioDevice.h"
#include "audio/Music.h"
#include "core/File.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <print>
#include <thread>

namespace mm2::tool {
namespace {

constexpr int kRate = 48000;

std::unique_ptr<vfs::Vfs> mountSource(const char* arg) {
    std::string err;
    auto source = vfs::probeGameSource(str::toPath(arg), &err);
    auto v = std::make_unique<vfs::Vfs>();
    if (!source || !source->usable() || !vfs::mountGameSource(*v, *source, &err)) {
        std::println(stderr, "error: not a usable game source: {}", err);
        return nullptr;
    }
    return v;
}

std::optional<audio::MusicState> parseState(std::string_view s) {
    using audio::MusicState;
    for (MusicState st : {MusicState::Silent, MusicState::Menu, MusicState::Racing, MusicState::Idle,
                          MusicState::IdleCops, MusicState::CopChase, MusicState::Paused, MusicState::Results})
        if (str::iequals(s, audio::toString(st)))
            return st;
    return std::nullopt;
}

bool writeWav(const std::filesystem::path& path, const std::vector<float>& stereo, int rate, int& clipped) {
    clipped = 0;
    std::vector<std::byte> out;
    auto put = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::byte*>(p);
        out.insert(out.end(), b, b + n);
    };
    auto u32 = [&](std::uint32_t v) { put(&v, 4); };
    auto u16 = [&](std::uint16_t v) { put(&v, 2); };
    const auto dataBytes = static_cast<std::uint32_t>(stereo.size() * 2);
    put("RIFF", 4);
    u32(36 + dataBytes);
    put("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 4));
    u16(4);
    u16(16);
    put("data", 4);
    u32(dataBytes);
    for (float f : stereo) {
        if (std::abs(f) > 1.0f)
            ++clipped;
        const auto s = static_cast<std::int16_t>(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
        u16(static_cast<std::uint16_t>(s));
    }
    return file::writeAtomic(path, out);
}

// Per-second RMS and peak, in dBFS.
void printStats(const std::vector<float>& stereo, int rate) {
    const std::size_t perSecond = static_cast<std::size_t>(rate) * 2;
    for (std::size_t start = 0, sec = 0; start < stereo.size(); start += perSecond, ++sec) {
        const std::size_t end = std::min(stereo.size(), start + perSecond);
        double sum = 0;
        float peak = 0;
        for (std::size_t i = start; i < end; ++i) {
            sum += static_cast<double>(stereo[i]) * stereo[i];
            peak = std::max(peak, std::abs(stereo[i]));
        }
        const double rms = std::sqrt(sum / static_cast<double>(std::max<std::size_t>(end - start, 1)));
        auto db = [](double v) { return v > 1e-9 ? 20.0 * std::log10(v) : -180.0; };
        std::println("  {:3}s  rms {:7.1f} dBFS  peak {:7.1f} dBFS", sec, db(rms), db(peak));
    }
}

int cmdMusicInfo(std::span<char* const> args) {
    if (args.empty())
        return 2;
    auto v = mountSource(args[0]);
    if (!v)
        return 1;
    const auto tables = audio::MusicTables::load(*v);
    audio::MusicLibrary lib(*v);
    std::println("{} DirectMusic files; menu '{}', ambience london '{}' sf '{}'", lib.files().size(), tables.menu,
                 tables.londonAmbience, tables.sfAmbience);
    int failures = 0;
    auto describe = [&](std::string_view label, const std::string& name) {
        if (name.empty())
            return;
        const auto t0 = std::chrono::steady_clock::now();
        const auto info = lib.info(name);
        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!info) {
            std::println("    {:9} {:22} FAILED", label, name);
            ++failures;
            return;
        }
        std::println("    {:9} {:22} length {:7.2f} s  repeats {:10}  load {:6.1f} ms", label, name, info->seconds,
                     info->repeats, ms);
    };
    auto songs = [&](const char* kind, const std::vector<audio::MusicSong>& list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& s = list[i];
            std::println("  {} song {}", kind, i);
            describe("start", s.start);
            describe("return", s.ret);
            describe("idle", s.idle);
            describe("idlecops", s.idleCops);
            describe("cops", s.cops);
            describe("pause", s.pause);
            describe("results", s.results);
            describe("motif", s.motifName);
        }
    };
    songs("race", tables.race);
    songs("cruise", tables.cruise);
    std::println("  other");
    describe("menu", tables.menu);
    describe("ambience", tables.londonAmbience);
    describe("ambience", tables.sfAmbience);
    describe("ambience", "UndergrounAmbience");
    return failures ? 1 : 0;
}

// music <source> <segment | --race N | --cruise N> [--states s:sec,...] [--ambience city]
//       [--bigair sec] [--seconds N] [--wav out.wav]
int cmdMusic(std::span<char* const> args) {
    if (args.size() < 2)
        return 2;
    auto v = mountSource(args[0]);
    if (!v)
        return 1;
    std::string segment;
    int song = -2;
    bool cruise = false;
    double seconds = 10.0;
    double bigAir = -1.0;
    bool profile = false;
    std::string wav, ambience, statesSpec;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : ""; };
        if (a == "--seconds")
            seconds = str::parseDouble(next()).value_or(seconds);
        else if (a == "--wav")
            wav = next();
        else if (a == "--race" || a == "--cruise") {
            cruise = a == "--cruise";
            song = static_cast<int>(str::parseInt(next()).value_or(-1));
        } else if (a == "--states")
            statesSpec = next();
        else if (a == "--ambience")
            ambience = next();
        else if (a == "--profile")
            profile = true;
        else if (a == "--bigair")
            bigAir = str::parseDouble(next()).value_or(-1.0);
        else if (!a.starts_with("--"))
            segment = std::string(a);
        else {
            std::println(stderr, "unknown option {}", a);
            return 2;
        }
    }

    // State script: "racing:10,idle:8,cop-chase:8" (state, then how long it lasts).
    std::vector<std::pair<audio::MusicState, double>> script;
    if (!statesSpec.empty()) {
        for (auto part : str::split(statesSpec, ',')) {
            const auto parts = str::split(part, ':');
            auto st = parseState(str::trim(parts[0]));
            if (!st) {
                std::println(stderr, "unknown state '{}'", parts[0]);
                return 2;
            }
            script.emplace_back(*st, parts.size() > 1 ? str::parseDouble(parts[1]).value_or(5.0) : 5.0);
        }
        seconds = 0;
        for (auto& [st, d] : script)
            seconds += d;
    } else if (song != -2) {
        script.emplace_back(audio::MusicState::Racing, seconds);
    }

    if (!wav.empty()) {
        // Offline: drive the engine directly, one block at a time.
        auto library = std::make_shared<audio::MusicLibrary>(*v);
        audio::MusicEngine engine(library, audio::MusicTables::load(*v), kRate);
        if (!segment.empty() && !engine.playSegment(segment))
            return 1;
        if (song != -2) {
            engine.selectSong(song, cruise);
            engine.preload();
        }
        if (!ambience.empty())
            engine.setAmbience(ambience);
        const auto total = static_cast<std::size_t>(seconds * kRate);
        constexpr int kBlock = 512;
        std::vector<float> mix(total * 2, 0.0f), music(kBlock * 2), amb(kBlock * 2);
        std::size_t scriptIndex = 0;
        double nextChange = 0.0;
        bool motifDone = bigAir < 0;
        double worstMs = 0.0, worstAt = 0.0;
        for (std::size_t frame = 0; frame < total; frame += kBlock) {
            const double t = static_cast<double>(frame) / kRate;
            if (scriptIndex < script.size() && t >= nextChange) {
                std::println("{:6.2f}s  -> {}", t, audio::toString(script[scriptIndex].first));
                engine.setState(script[scriptIndex].first);
                nextChange += script[scriptIndex].second;
                ++scriptIndex;
            }
            if (!motifDone && t >= bigAir) {
                std::println("{:6.2f}s  -> big air motif", t);
                engine.triggerMotif();
                motifDone = true;
            }
            const int n = static_cast<int>(std::min<std::size_t>(kBlock, total - frame));
            const auto t0 = std::chrono::steady_clock::now();
            engine.render(music.data(), amb.data(), n);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (profile && ms > worstMs) {
                worstMs = ms;
                worstAt = t;
            }
            for (int i = 0; i < n * 2; ++i)
                mix[frame * 2 + static_cast<std::size_t>(i)] = music[static_cast<std::size_t>(i)] + amb[static_cast<std::size_t>(i)];
        }
        int clipped = 0;
        if (!writeWav(str::toPath(wav), mix, kRate, clipped)) {
            std::println(stderr, "error: cannot write {}", wav);
            return 1;
        }
        std::println("wrote {} ({:.1f} s, {} clipped samples)", wav, seconds, clipped);
        if (profile)
            std::println("slowest {}-frame block: {:.2f} ms at {:.2f} s (real time allows {:.2f} ms)", kBlock, worstMs,
                         worstAt, 1000.0 * kBlock / kRate);
        printStats(mix, kRate);
        return 0;
    }

    // Live: through the mixer and the default audio device.
    auto mixer = std::make_shared<audio::Mixer>(kRate);
    audio::MusicPlayer player(*v, kRate);
    mixer->addStream(player.musicStream(), audio::Bus::Music);
    mixer->addStream(player.ambienceStream(), audio::Bus::Music);
    audio::AudioDevice device;
    std::string err;
    if (!device.open(mixer, &err)) {
        std::println(stderr, "error: audio device: {}", err);
        return 1;
    }
    if (!ambience.empty())
        player.setAmbience(ambience);
    if (!segment.empty())
        player.playSegment(segment);
    if (song != -2)
        player.startRace(song, cruise);
    // Same state script semantics as the offline render.
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    double nextChange = 0;
    std::size_t scriptIndex = 0;
    bool motifDone = bigAir < 0;
    while (elapsed() < seconds) {
        if (scriptIndex < script.size() && elapsed() >= nextChange) {
            std::println("{:6.2f}s  -> {}", elapsed(), audio::toString(script[scriptIndex].first));
            if (scriptIndex > 0 || script[0].first != audio::MusicState::Racing) // startRace() already races
                player.setState(script[scriptIndex].first);
            nextChange += script[scriptIndex].second;
            ++scriptIndex;
        }
        if (!motifDone && elapsed() >= bigAir) {
            std::println("{:6.2f}s  -> big air motif", elapsed());
            player.triggerBigAir();
            motifDone = true;
        }
        SDL_Delay(10);
    }
    std::println("underruns: {}", player.underruns());
    return 0;
}

const Registrar r1({"musicinfo", "<game-source>", "load every soundtrack segment and print lengths", &cmdMusicInfo});
const Registrar r2({"music",
                    "<game-source> <segment | --race N | --cruise N> [--states state:sec,...] "
                    "[--ambience london|sf|underground] [--bigair sec] [--seconds N] [--wav out.wav [--profile]]",
                    "render (--wav) or play the DirectMusic soundtrack", &cmdMusic});

} // namespace
} // namespace mm2::tool
