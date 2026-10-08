#pragma once

// Interactive DirectMusic soundtrack (aud/dmusic/ in the retail archives),
// played with GothicKit/dmusic. See docs/music.md.

#include "audio/Mixer.h"
#include "vfs/Vfs.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

struct DmLoader;
struct DmPerformance;
struct DmSegment;
struct DmStyle;

namespace mm2::audio {

// One song of the in-game soundtrack: a row of singlerace.csv (races) or
// singleroam.csv (cruise). Values are segment names without extension
// ("EnemyStart" -> aud/dmusic/enemystart.sgt); case differs between files.
struct MusicSong {
    std::string start;    // "Start Music"
    std::string ret;      // "Return Music" (race) / restart (cruise)
    std::string idle;     // "Idle Race Music" / "Idle Music"
    std::string idleCops; // "Idle Cop Music"
    std::string cops;     // "Cop chase music"
    std::string pause;    // "Pause Music"
    std::string results;  // "Race results Music" (race table only)
    std::string motifStyle, motifName, motifBand; // "Big air" motif
};

// The music tables in aud/dmusic/csv_files/.
struct MusicTables {
    std::vector<MusicSong> race;   // singlerace.csv
    std::vector<MusicSong> cruise; // singleroam.csv
    std::string menu;              // ui.csv ("UI")
    std::string londonAmbience;    // londonambience.csv
    std::string sfAmbience;        // sfambience.csv

    static std::vector<MusicSong> parseRace(std::string_view text);
    static std::vector<MusicSong> parseCruise(std::string_view text);
    // ui.csv / *ambience.csv: a header line followed by one segment name.
    static std::string parseSingle(std::string_view text);
    // mmGameMusicData::RandomizeNumber: the song drawn from `count` rows.
    static int pickSong(int count);
    static MusicTables load(const vfs::Vfs& vfs);
};

// Loads DirectMusic files from the game's virtual file system. References
// inside the files are bare names with inconsistent case and spaces
// ("DLS Collection1.dls"); lookups are case-insensitive by base name.
// Thread-safe.
class MusicLibrary {
public:
    explicit MusicLibrary(const vfs::Vfs& vfs);
    ~MusicLibrary();
    MusicLibrary(const MusicLibrary&) = delete;
    MusicLibrary& operator=(const MusicLibrary&) = delete;

    bool ok() const { return m_loader != nullptr; }
    DmLoader* loader() const { return m_loader; }
    // Every DirectMusic file found (virtual paths).
    const std::vector<std::string>& files() const { return m_files; }

    // Loads and downloads (resolves styles, bands, DLS) a segment by name, with
    // or without ".sgt". Cached; returns a new reference or nullptr. Can take
    // tens of milliseconds for a segment with large DLS collections.
    DmSegment* segment(std::string_view name);
    bool isCached(std::string_view name) const;

    struct SegmentInfo {
        double seconds = 0;        // length of one repeat
        std::uint32_t repeats = 0; // DirectMusic repeat count (large = loops "forever")
    };
    // Loads the segment (see segment()) and reports its length.
    std::optional<SegmentInfo> info(std::string_view name);

    // Raw file bytes by (case-insensitive) base name, or nullopt.
    std::optional<std::vector<std::byte>> readFile(std::string_view name) const;

    // Some bands take instruments from the system General MIDI collection
    // (gm.dls, the Roland sound set Windows ships in System32\drivers), which
    // is not part of the game data. It is looked up at $OPENMM2_GM_DLS, then in
    // the Windows directory. Without it those instruments are silent.
    bool hasGeneralMidi() const { return !m_gmPath.empty(); }

private:
    static void* resolve(void* ctx, const char* file, std::size_t* len);
    std::string key(std::string_view name) const;

    const vfs::Vfs& m_vfs;
    std::unordered_map<std::string, std::string> m_index; // lower-case base name -> virtual path
    std::vector<std::string> m_files;
    std::filesystem::path m_gmPath;
    DmLoader* m_loader = nullptr;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, DmSegment*> m_cache; // lower-case "name.sgt" -> segment
};

// Which music the game wants. MusicDirector (MusicDirector.h) ports MM2's
// rules for when each is chosen; see docs/music.md.
enum class MusicState {
    Silent,
    Menu,     // frontend ("UI")
    Racing,   // Start, then Return after leaving another state (tools)
    Idle,     // player slow for a while
    IdleCops, // idle during a cop chase
    CopChase, // police pursuing the player
    Paused,   // pause menu
    Results,  // race results
    Start,    // the song's Start segment
    Return,   // the song's Return / Restart segment
};

const char* toString(MusicState s);

// When a state change takes effect. MM2's DMusicObject::SegmentSwitch(int)
// starts the new segment on the next beat (DMUS_SEGF_BEAT) and its composer
// transitions (AutoTransition with DMUS_COMPOSEF_MEASURE) on the next measure.
// Auto keeps OpenMM2's default for setState() callers that do not say.
enum class MusicTiming { Auto, Immediate, Beat, Measure };

// Single-threaded music core: one performance for the soundtrack, one for the
// "Big Air" motif (DirectMusic played it as a secondary segment; dmusic has no
// secondary segments, so it is layered on its own performance) and one for city
// ambience. Not thread-safe; MusicPlayer drives it from a worker thread, tools
// and tests drive it directly.
class MusicEngine {
public:
    MusicEngine(std::shared_ptr<MusicLibrary> library, MusicTables tables, int sampleRate);
    ~MusicEngine();
    MusicEngine(const MusicEngine&) = delete;
    MusicEngine& operator=(const MusicEngine&) = delete;

    const MusicTables& tables() const { return m_tables; }
    MusicLibrary& library() { return *m_library; }
    int sampleRate() const { return m_rate; }

    // Selects the song for Racing/Idle/... states. `cruise` picks from the
    // cruise table. song < 0 draws one (MusicTables::pickSong).
    void selectSong(int song, bool cruise);
    int song() const { return m_song; }
    bool cruise() const { return m_cruise; }
    void setState(MusicState state, MusicTiming timing = MusicTiming::Auto);
    MusicState state() const { return m_state; }
    // The "Big Air" motif. DMusicObject::PlayMotif sets one repeat, so it
    // plays twice.
    void triggerMotif();
    // "london", "sf", "underground" or "" to stop.
    void setAmbience(std::string_view which);
    // Plays a single segment by name on the music performance (tools).
    bool playSegment(std::string_view name);
    void stopAll();

    // Loads every segment the current song and ambience may need, so later
    // state changes never stall rendering.
    void preload();

    // Renders `frames` stereo frames of music and of ambience (either pointer may be null).
    void render(float* music, float* ambience, int frames);

private:
    const MusicSong* currentSong() const;
    std::string segmentFor(MusicState state) const;
    void transitionTo(const std::string& segment, MusicTiming timing);
    bool loadMotifStyle();
    void startMotif();
    void renderMotif(float* out, int frames);

    std::shared_ptr<MusicLibrary> m_library;
    MusicTables m_tables;
    int m_rate;
    DmPerformance* m_music = nullptr;
    DmPerformance* m_motif = nullptr;
    DmPerformance* m_ambience = nullptr;
    int m_song = 0;
    bool m_cruise = false;
    MusicState m_state = MusicState::Silent;
    bool m_startedSong = false;     // the song's Start segment has played
    std::string m_playing;           // segment currently on m_music
    DmStyle* m_motifStyle = nullptr;
    std::string m_motifStyleName;
    std::int64_t m_motifDelay = -1;    // frames until a pending motif starts (-1: none)
    std::int64_t m_motifRemaining = 0; // frames until the motif is stopped
    int m_motifRepeats = 0;            // plays left after the current one
    std::string m_ambienceSegment;
    std::vector<float> m_scratch;
};

// Threaded front end: a worker thread renders the engine ahead into two
// ring buffers (music, ambience) that the mixer pulls via StreamSources.
// All methods are safe to call from the game thread; they enqueue commands.
// Latency from a call to audible output is at most the buffered amount
// (kBufferFrames, ~85 ms at 48 kHz) plus the musical boundary the
// transition waits for.
class MusicPlayer {
public:
    static constexpr int kChunkFrames = 512;
    static constexpr int kBufferFrames = 4096;

    MusicPlayer(const vfs::Vfs& vfs, int sampleRate);
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    bool ok() const { return m_ok; }
    const MusicTables& tables() const { return m_tables; }

    // Add these to the mixer, e.g. mixer.addStream(p.musicStream(), Bus::Music).
    // The original had one "Music/City" volume for both.
    std::shared_ptr<StreamSource> musicStream() { return m_musicStream; }
    std::shared_ptr<StreamSource> ambienceStream() { return m_ambienceStream; }

    void playMenu();
    // song < 0: random. Preloads the song's segments in the background, then
    // plays Start, or with play = false only selects the song (a
    // MusicDirector then starts it).
    void startRace(int song, bool cruise, bool play = true);
    void setState(MusicState state, MusicTiming timing = MusicTiming::Auto);
    void triggerBigAir();
    void setAmbience(std::string_view which);
    void playSegment(std::string_view name);
    void stop();

    std::uint64_t underruns() const { return m_underruns.load(std::memory_order_relaxed); }

private:
    class Ring;
    class Stream;
    void worker();
    void loader();
    std::vector<std::string> songSegments(int song, bool cruise) const;
    void post(std::function<void(MusicEngine&)> cmd);
    // Loads `names` on the loader thread, then runs `then` on the worker.
    void loadThen(std::vector<std::string> names, std::function<void(MusicEngine&)> then);

    MusicTables m_tables;
    std::shared_ptr<MusicLibrary> m_library;
    std::unique_ptr<MusicEngine> m_engine;
    std::shared_ptr<Ring> m_musicRing, m_ambienceRing;
    std::shared_ptr<StreamSource> m_musicStream, m_ambienceStream;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void(MusicEngine&)>> m_commands;
    std::atomic<bool> m_quit{false};
    std::atomic<std::uint64_t> m_underruns{0};
    struct LoadRequest {
        std::vector<std::string> names;
        std::function<void(MusicEngine&)> then;
    };
    std::mutex m_loadMutex;
    std::condition_variable m_loadWake;
    std::deque<LoadRequest> m_loads;
    std::thread m_thread;
    std::thread m_loaderThread;
    bool m_ok = false;
};

} // namespace mm2::audio
