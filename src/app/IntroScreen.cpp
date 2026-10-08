// Intro movie: Indeo 5 video decoded on a worker thread, PCM soundtrack played
// through the mixer, video timed by the audio clock.
#include "app/IntroScreen.h"

#include "app/Screens.h"
#include "core/Log.h"
#include "render/Device.h"
#include "video/Movie.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mm2::app {
namespace {

constexpr const char* kMovieFile = "LOGOS.AVI";

// Plays a PCM buffer once, resampled linearly to the mixer rate, and exposes
// its position as the clock the video follows.
class PcmStream final : public audio::StreamSource {
public:
    PcmStream(audio::SoundBuffer sound, int outputRate)
        : m_sound(std::move(sound)), m_step(static_cast<double>(m_sound.sampleRate) / outputRate) {}

    void render(float* stereo, int frames) override {
        if (m_paused.load(std::memory_order_relaxed)) {
            std::fill(stereo, stereo + static_cast<std::ptrdiff_t>(frames) * 2, 0.0f);
            return;
        }
        const std::size_t total = m_sound.frames();
        const int ch = m_sound.channels;
        double pos = m_pos.load(std::memory_order_relaxed);
        for (int i = 0; i < frames; ++i) {
            const auto idx = static_cast<std::size_t>(pos);
            float l = 0.0f, r = 0.0f;
            if (idx < total) {
                const std::size_t next = std::min(idx + 1, total - 1);
                const float frac = static_cast<float>(pos - static_cast<double>(idx));
                auto sample = [&](std::size_t f, int c) {
                    return static_cast<float>(m_sound.samples[f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)]) *
                           (1.0f / 32768.0f);
                };
                l = sample(idx, 0) + (sample(next, 0) - sample(idx, 0)) * frac;
                r = ch > 1 ? sample(idx, 1) + (sample(next, 1) - sample(idx, 1)) * frac : l;
                pos += m_step;
            }
            stereo[i * 2] = l;
            stereo[i * 2 + 1] = r;
        }
        m_pos.store(pos, std::memory_order_relaxed);
    }

    double seconds() const { return m_pos.load(std::memory_order_relaxed) / m_sound.sampleRate; }
    bool finished() const { return m_pos.load(std::memory_order_relaxed) >= static_cast<double>(m_sound.frames()); }
    void setPaused(bool paused) { m_paused.store(paused, std::memory_order_relaxed); }

private:
    audio::SoundBuffer m_sound;
    double m_step;
    std::atomic<double> m_pos{0.0};
    std::atomic<bool> m_paused{false};
};

class IntroScreen final : public Screen {
public:
    explicit IntroScreen(Context& ctx) : m_ctx(ctx) {
        if (!ctx.game) {
            m_done = true;
            return;
        }
        std::string error;
        auto file = vfs::openSourceFile(ctx.game->source, kMovieFile, &error);
        if (file)
            m_movie = video::Movie::open(std::move(file), &error);
        if (!m_movie) {
            log::info("intro: {} unavailable ({}); skipping", kMovieFile, error);
            m_done = true;
            return;
        }
        log::info("intro: {} {}x{} {:.1f} fps, {} frames", kMovieFile, m_movie->width(), m_movie->height(),
                  m_movie->fps(), m_movie->frameCount());

        render::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(m_movie->width());
        desc.height = static_cast<std::uint32_t>(m_movie->height());
        desc.debugName = "intro movie";
        m_texture = ctx.device().createTexture(desc);

        // The audio clock drives playback when an output device is running.
        if (auto sound = m_movie->readAudio(&error); sound && ctx.mixer && ctx.audioDevice.isOpen()) {
            m_audio = std::make_shared<PcmStream>(std::move(*sound), ctx.mixer->sampleRate());
            m_streamId = ctx.mixer->addStream(m_audio, audio::Bus::Effects);
        }
        m_worker = std::thread([this] { decodeLoop(); });
    }

    ~IntroScreen() override {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        if (m_worker.joinable())
            m_worker.join();
        if (m_streamId && m_ctx.mixer)
            m_ctx.mixer->removeStream(m_streamId);
        if (m_texture)
            m_ctx.device().destroyTexture(m_texture);
    }

    void update(Context& ctx, double dt) override {
        if (m_done) {
            finish(ctx);
            return;
        }
        // ebolaPlayMovie pauses the movie while the game is not the active
        // application and resumes it when it is again. (Only once the window
        // has had focus: a window that never got it was not deactivated.)
        const bool focused = ctx.renderer.window && ctx.window().focused();
        m_hadFocus = m_hadFocus || focused;
        const bool paused = m_hadFocus && !focused;
        if (m_audio)
            m_audio->setPaused(paused);
        if (paused)
            return;
        // ebolaPlayMovie checks Esc, Space and the left mouse button (pressed
        // since the last check) every 250 ms.
        m_skipPending = m_skipPending || skipPressed(ctx);
        m_pollClock += dt;
        if (m_pollClock >= kSkipPollSeconds) {
            m_pollClock = 0.0;
            if (m_skipPending) {
                finish(ctx);
                return;
            }
        }
        m_wallClock += dt;
        const double t = m_audio ? m_audio->seconds() : m_wallClock;
        const int target = static_cast<int>(t * m_movie->fps());

        // Show the latest decoded frame that is due.
        std::vector<std::uint8_t> show;
        {
            std::lock_guard lock(m_mutex);
            while (!m_queue.empty() && m_queue.front().index <= target) {
                show = std::move(m_queue.front().rgba);
                m_shown = m_queue.front().index;
                m_queue.pop_front();
            }
        }
        m_wake.notify_all();
        if (!show.empty()) {
            const render::Rect all{0, 0, static_cast<std::uint32_t>(m_movie->width()),
                                   static_cast<std::uint32_t>(m_movie->height())};
            ctx.device().updateTexture(m_texture, 0, all, show.data());
            m_hasFrame = true;
        }
        if (static_cast<int>(m_wallClock) != m_lastLogSecond) {
            m_lastLogSecond = static_cast<int>(m_wallClock);
            log::debug("intro: wall {:.1f} s, clock {:.2f} s, frame {}/{}, shown {}, decoded all {}", m_wallClock, t,
                       target, m_movie->frameCount(), m_shown, m_decodeFinished.load());
        }
        // Done once the last frame is on screen and the soundtrack has ended
        // (LOGOS.AVI's audio is a fraction of a frame shorter than its video),
        // or, without audio, when the last frame's display time has passed. The
        // wall-clock limit guards against a stalled audio device.
        const double duration = m_movie->frameCount() / m_movie->fps();
        const bool lastShown = m_decodeFinished.load() && m_shown >= m_movie->frameCount() - 1;
        const bool clockDone = m_audio ? m_audio->finished() : t >= duration;
        if ((lastShown && clockDone) || m_wallClock > duration + 2.0)
            finish(ctx);
    }

    void drawOverlay(Context& ctx) override {
        auto& ov = *ctx.overlay;
        ov.begin(ctx.display.uiScale);
        const auto& l = ov.layout();
        ov.rect(l.left, l.top, l.right - l.left, l.bottom - l.top, render::packColor(0, 0, 0));
        if (m_hasFrame && m_movie) {
            // ebolaPlayMovie: the MCI window at the movie's own size (320 x 240
            // for LOGOS.AVI), centred on the 640 x 480 screen the game has
            // just switched to (gfxPipeline::SetRes before the movie).
            const float w = std::min(640.0f, static_cast<float>(m_movie->width()));
            const float h = std::min(480.0f, static_cast<float>(m_movie->height()));
            ov.image(m_texture, (640.0f - w) * 0.5f, (480.0f - h) * 0.5f, w, h, {0, 0}, {1, 1}, 0xFFFFFFFFu,
                     render::BlendMode::Opaque, render::Filter::Bilinear);
        }
        ov.end();
    }

private:
    struct Decoded {
        int index;
        std::vector<std::uint8_t> rgba;
    };

    static bool skipPressed(Context& ctx) {
        const auto& in = ctx.input;
        return in.keyPressed(platform::Key::Escape) || in.keyPressed(platform::Key::Space) ||
               in.mousePressed(platform::MouseButton::Left);
    }

    void finish(Context& ctx) {
        if (m_finished)
            return;
        m_finished = true;
        ctx.nextScreen = makeFrontendScreen(ctx);
    }

    void decodeLoop() {
        constexpr std::size_t kAhead = 6;
        for (int i = 0; i < m_movie->frameCount(); ++i) {
            Decoded d{i, {}};
            std::string error;
            if (!m_movie->decodeNextRgba(d.rgba, &error))
                break;
            if (!error.empty())
                log::warn("intro: {}", error);
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&] { return m_quit || m_queue.size() < kAhead; });
            if (m_quit)
                return;
            m_queue.push_back(std::move(d));
        }
        m_decodeFinished = true;
    }

    Context& m_ctx;
    std::unique_ptr<video::Movie> m_movie;
    render::TextureHandle m_texture;
    std::shared_ptr<PcmStream> m_audio;
    int m_streamId = 0;
    static constexpr double kSkipPollSeconds = 0.25;

    double m_wallClock = 0.0;
    double m_pollClock = 0.0;
    bool m_skipPending = false;
    bool m_hadFocus = false;
    bool m_done = false;
    bool m_finished = false;
    bool m_hasFrame = false;
    int m_shown = -1;
    int m_lastLogSecond = -1;

    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Decoded> m_queue;
    bool m_quit = false;
    std::atomic<bool> m_decodeFinished{false};
};

} // namespace

std::unique_ptr<Screen> makeIntroScreen(Context& ctx) { return std::make_unique<IntroScreen>(ctx); }

} // namespace mm2::app
