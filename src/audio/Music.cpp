#include "audio/Music.h"

#include "audio/MusicMotif.h"

#include "core/File.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <dmusic.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

namespace mm2::audio {
namespace {

std::string_view asText(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Lower-cased base name of a reference ("..\\Music\\DLS Collection1.dls" ->
// "dls collection1.dls").
std::string baseKey(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    return str::lower(str::trim(name));
}

std::string withSgt(std::string_view name) {
    std::string k = baseKey(name);
    if (!k.ends_with(".sgt"))
        k += ".sgt";
    return k;
}

void dmLog(void*, DmLogLevel lvl, const char* msg) {
    // dmusic reports releasing a NULL segment (stopping when nothing plays) as an error.
    if (lvl <= DmLogLevel_ERROR && !std::strstr(msg, "NULL passed to"))
        log::warn("dmusic: {}", msg);
    else
        log::debug("dmusic: {}", msg);
}

// DirectMusic chose pattern and note variations with rand(), so the music
// differs between plays; dmusic's default uses rand() without seeding, which
// repeats every run. OPENMM2_MUSIC_SEED fixes the seed for reproducible renders.
std::uint32_t dmRandom(void*) {
    static std::mutex mutex;
    static std::mt19937 rng = [] {
        const char* seed = std::getenv("OPENMM2_MUSIC_SEED");
        if (seed && *seed)
            return std::mt19937(static_cast<std::uint32_t>(std::strtoul(seed, nullptr, 10)));
        return std::mt19937(std::random_device{}());
    }();
    std::lock_guard lock(mutex);
    return static_cast<std::uint32_t>(rng());
}

// dmusic's logger and random generator are global, so they are set up once.
// The logger formats into a shared buffer. Its warnings (mostly unsupported DLS articulations) go to the debug
// log; everything down to per-note tracing is available with
// OPENMM2_DMUSIC_TRACE=1 or at trace log level.
void initDmusicGlobals() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char* trace = std::getenv("OPENMM2_DMUSIC_TRACE");
        const bool verbose = (trace && *trace) || log::enabled(log::Level::Trace);
        Dm_setLogger(verbose ? DmLogLevel_TRACE : DmLogLevel_WARN, &dmLog, nullptr);
        Dm_setRandomNumberGenerator(&dmRandom, nullptr);
    });
}

// dmusic's synthesizer (TinySoundFont) renders MM2's songs with peaks above
// full scale. The original's absolute music level (Microsoft software synth)
// is not known; this headroom keeps the soundtrack below clipping and leaves
// the balance against effects to the music volume setting.
constexpr float kSynthGain = 0.5f;

DmPerformance* createPerformance(int rate) {
    DmPerformance* p = nullptr;
    if (DmPerformance_create(&p, static_cast<std::uint32_t>(rate)) != DmResult_SUCCESS) {
        log::error("music: cannot create a DirectMusic performance");
        return nullptr;
    }
    OpenMM2_DmPerformance_setDefaultChord(p);
    DmPerformance_setVolume(p, kSynthGain);
    return p;
}

MusicSong songFromRow(const std::vector<std::string>& row, bool race) {
    auto at = [&](std::size_t i) { return i < row.size() ? row[i] : std::string(); };
    MusicSong s;
    if (race) {
        // Start, Return, Idle Race, Idle Cop, Cop chase, Pause, Results, motif style/name/band
        s.start = at(0);
        s.ret = at(1);
        s.idle = at(2);
        s.idleCops = at(3);
        s.cops = at(4);
        s.pause = at(5);
        s.results = at(6);
        s.motifStyle = at(7);
        s.motifName = at(8);
        s.motifBand = at(9);
    } else {
        // Start, Return, Idle, Cop Chase, idle cop, Pause, motif style/name/band
        s.start = at(0);
        s.ret = at(1);
        s.idle = at(2);
        s.cops = at(3);
        s.idleCops = at(4);
        s.pause = at(5);
        s.motifStyle = at(6);
        s.motifName = at(7);
        s.motifBand = at(8);
    }
    return s;
}

std::vector<MusicSong> parseSongs(std::string_view text, bool race) {
    std::vector<MusicSong> songs;
    const auto table = data::CsvTable::parse(text, true);
    for (const auto& row : table.rows()) {
        if (row.empty() || row[0].empty())
            continue;
        songs.push_back(songFromRow(row, race));
    }
    return songs;
}

std::filesystem::path findGeneralMidi() {
    std::error_code ec;
    if (const char* env = std::getenv("OPENMM2_GM_DLS"); env && *env && std::filesystem::is_regular_file(env, ec))
        return env;
#ifdef _WIN32
    if (const char* windir = std::getenv("WINDIR")) {
        const auto p = std::filesystem::path(windir) / "System32" / "drivers" / "gm.dls";
        if (std::filesystem::is_regular_file(p, ec))
            return p;
    }
#endif
    return {};
}

// Ambience segment for a setAmbience() name.
std::string ambienceSegment(const MusicTables& t, std::string_view which) {
    if (str::iequals(which, "london"))
        return t.londonAmbience;
    if (str::iequals(which, "sf"))
        return t.sfAmbience;
    if (str::iequals(which, "underground"))
        return "UndergrounAmbience"; // sic: the retail file is UndergrounAmbience.sgt
    return {};
}

} // namespace

// --- MusicTables -------------------------------------------------------------

std::vector<MusicSong> MusicTables::parseRace(std::string_view text) { return parseSongs(text, true); }
std::vector<MusicSong> MusicTables::parseCruise(std::string_view text) { return parseSongs(text, false); }

std::string MusicTables::parseSingle(std::string_view text) {
    const auto lines = data::splitLines(text);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto cell = str::trim(str::split(lines[i], ',')[0]);
        if (!cell.empty())
            return std::string(cell);
    }
    return {};
}

MusicTables MusicTables::load(const vfs::Vfs& vfs) {
    MusicTables t;
    auto read = [&](const char* name) -> std::string {
        auto bytes = vfs.readAll(std::string("aud/dmusic/csv_files/") + name);
        if (!bytes) {
            log::warn("music: aud/dmusic/csv_files/{} missing", name);
            return {};
        }
        return std::string(asText(*bytes));
    };
    t.race = parseRace(read("singlerace.csv"));
    t.cruise = parseCruise(read("singleroam.csv"));
    t.menu = parseSingle(read("ui.csv"));
    t.londonAmbience = parseSingle(read("londonambience.csv"));
    t.sfAmbience = parseSingle(read("sfambience.csv"));
    return t;
}

// --- MusicLibrary ------------------------------------------------------------

MusicLibrary::MusicLibrary(const vfs::Vfs& vfs) : m_vfs(vfs) {
    initDmusicGlobals();
    for (const auto& e : vfs.listFiles()) {
        if (!e.path.starts_with("aud/dmusic/") || e.path.find("/csv_files/") != std::string::npos)
            continue;
        m_index.try_emplace(baseKey(e.path), e.path);
        m_files.push_back(e.path);
    }
    m_gmPath = findGeneralMidi();
    if (m_gmPath.empty())
        log::info("music: no General MIDI collection (gm.dls); a few instruments will be silent");
    else
        log::info("music: General MIDI instruments from {}", str::fromPath(m_gmPath));
    if (DmLoader_create(&m_loader, DmLoader_DOWNLOAD) != DmResult_SUCCESS ||
        DmLoader_addResolver(m_loader, &MusicLibrary::resolve, this) != DmResult_SUCCESS) {
        log::error("music: cannot create the DirectMusic loader");
        DmLoader_release(m_loader);
        m_loader = nullptr;
    }
    log::debug("music: {} DirectMusic files", m_files.size());
}

MusicLibrary::~MusicLibrary() {
    for (auto& [name, sgt] : m_cache)
        DmSegment_release(sgt);
    DmLoader_release(m_loader);
}

std::string MusicLibrary::key(std::string_view name) const { return withSgt(name); }

std::optional<std::vector<std::byte>> MusicLibrary::readFile(std::string_view name) const {
    const auto it = m_index.find(baseKey(name));
    if (it == m_index.end())
        return std::nullopt;
    return m_vfs.readAll(it->second);
}

void* MusicLibrary::resolve(void* ctx, const char* file, std::size_t* len) {
    auto* self = static_cast<MusicLibrary*>(ctx);
    auto bytes = self->readFile(file);
    const std::string key = baseKey(file);
    // Bands that use General MIDI instruments reference the system collection.
    // Without it the loader reports "not found" and (with OpenMM2's dmusic
    // patch) only those instruments are silent.
    if (!bytes && key == "gm.dls" && !self->m_gmPath.empty())
        bytes = file::readBinary(self->m_gmPath);
    if (!bytes) {
        log::debug("music: '{}' not found", file);
        return nullptr;
    }
    // Ownership passes to dmusic, which frees with free().
    void* buf = std::malloc(bytes->size());
    if (!buf)
        return nullptr;
    std::memcpy(buf, bytes->data(), bytes->size());
    *len = bytes->size();
    return buf;
}

bool MusicLibrary::isCached(std::string_view name) const {
    std::lock_guard lock(m_mutex);
    return m_cache.contains(key(name));
}

DmSegment* MusicLibrary::segment(std::string_view name) {
    if (!m_loader || name.empty())
        return nullptr;
    const std::string k = key(name);
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_cache.find(k); it != m_cache.end())
            return DmSegment_retain(it->second);
    }
    // Load outside our lock; the loader has its own.
    DmSegment* sgt = nullptr;
    const DmResult rv = DmLoader_getSegment(m_loader, k.c_str(), &sgt);
    if (rv != DmResult_SUCCESS || !sgt) {
        log::warn("music: cannot load segment '{}' (dmusic error {})", name, static_cast<int>(rv));
        return nullptr;
    }
    std::lock_guard lock(m_mutex);
    auto [it, inserted] = m_cache.try_emplace(k, sgt);
    if (!inserted) {
        DmSegment_release(sgt); // another thread loaded it first
        sgt = it->second;
    }
    return DmSegment_retain(sgt);
}

std::optional<MusicLibrary::SegmentInfo> MusicLibrary::info(std::string_view name) {
    DmSegment* sgt = segment(name);
    if (!sgt)
        return std::nullopt;
    SegmentInfo i{DmSegment_getLength(sgt), DmSegment_getRepeats(sgt)};
    DmSegment_release(sgt);
    return i;
}

// --- MusicEngine -------------------------------------------------------------

const char* toString(MusicState s) {
    switch (s) {
    case MusicState::Silent: return "silent";
    case MusicState::Menu: return "menu";
    case MusicState::Racing: return "racing";
    case MusicState::Idle: return "idle";
    case MusicState::IdleCops: return "idle-cops";
    case MusicState::CopChase: return "cop-chase";
    case MusicState::Paused: return "paused";
    case MusicState::Results: return "results";
    case MusicState::Start: return "start";
    case MusicState::Return: return "return";
    }
    return "?";
}

MusicEngine::MusicEngine(std::shared_ptr<MusicLibrary> library, MusicTables tables, int sampleRate)
    : m_library(std::move(library)), m_tables(std::move(tables)), m_rate(sampleRate) {
    m_music = createPerformance(m_rate);
    m_motif = createPerformance(m_rate);
    m_ambience = createPerformance(m_rate);
}

MusicEngine::~MusicEngine() {
    OpenMM2_DmStyle_release(m_motifStyle);
    DmPerformance_release(m_music);
    DmPerformance_release(m_motif);
    DmPerformance_release(m_ambience);
}

const MusicSong* MusicEngine::currentSong() const {
    const auto& list = m_cruise ? m_tables.cruise : m_tables.race;
    if (list.empty())
        return nullptr;
    return &list[static_cast<std::size_t>(m_song) % list.size()];
}

void MusicEngine::selectSong(int song, bool cruise) {
    m_cruise = cruise;
    const auto& list = cruise ? m_tables.cruise : m_tables.race;
    if (song < 0 && !list.empty()) {
        static std::mt19937 rng{std::random_device{}()};
        song = static_cast<int>(rng() % list.size());
    }
    m_song = std::max(song, 0);
    m_startedSong = false;
}

std::string MusicEngine::segmentFor(MusicState state) const {
    if (state == MusicState::Menu)
        return m_tables.menu;
    const MusicSong* s = currentSong();
    if (!s)
        return {};
    switch (state) {
    case MusicState::Racing: return m_startedSong ? s->ret : s->start;
    case MusicState::Start: return s->start;
    case MusicState::Return: return s->ret;
    case MusicState::Idle: return s->idle;
    case MusicState::IdleCops: return s->idleCops.empty() ? s->idle : s->idleCops;
    case MusicState::CopChase: return s->cops;
    case MusicState::Paused: return s->pause;
    case MusicState::Results: return s->results;
    default: return {};
    }
}

void MusicEngine::preload() {
    if (const MusicSong* s = currentSong())
        for (const std::string* name : {&s->start, &s->ret, &s->idle, &s->idleCops, &s->cops, &s->pause,
                                        &s->results, &s->motifName})
            if (!name->empty() && name != &s->motifName)
                DmSegment_release(m_library->segment(*name));
    if (!m_tables.menu.empty())
        DmSegment_release(m_library->segment(m_tables.menu));
    loadMotifStyle();
}

bool MusicEngine::loadMotifStyle() {
    const MusicSong* s = currentSong();
    if (!s || s->motifStyle.empty())
        return false;
    const std::string file = baseKey(s->motifStyle) + ".sty";
    if (m_motifStyle && m_motifStyleName == file)
        return true;
    OpenMM2_DmStyle_release(m_motifStyle);
    m_motifStyle = OpenMM2_DmStyle_load(m_library->loader(), file.c_str());
    m_motifStyleName = m_motifStyle ? file : std::string();
    if (!m_motifStyle)
        log::warn("music: cannot load motif style '{}'", s->motifStyle);
    return m_motifStyle != nullptr;
}

void MusicEngine::transitionTo(const std::string& name, MusicTiming timing) {
    if (!m_music)
        return;
    if (name.empty()) {
        DmPerformance_playSegment(m_music, nullptr, DmTiming_INSTANT);
        m_playing.clear();
        return;
    }
    // DMusicObject::SegmentSwitch does nothing for the segment already playing.
    if (str::iequals(name, m_playing))
        return;
    DmSegment* sgt = m_library->segment(name);
    if (!sgt)
        return;
    // MM2's composer transitions use the groove command without embellishment
    // (DMUS_COMMANDT_GROOVE); dmusic's composed FILL transitions are silent for
    // styles without fill patterns, so the new segment simply starts on the
    // boundary.
    DmTiming t = DmTiming_INSTANT;
    switch (timing) {
    case MusicTiming::Beat: t = DmTiming_BEAT; break;
    case MusicTiming::Measure: t = DmTiming_MEASURE; break;
    default: break;
    }
    // Nothing playing: start at once (there is no beat to wait for).
    if (m_playing.empty())
        t = DmTiming_INSTANT;
    DmPerformance_playSegment(m_music, sgt, t);
    DmSegment_release(sgt);
    m_playing = name;
}

void MusicEngine::setState(MusicState state, MusicTiming timing) {
    const MusicState previous = m_state;
    m_state = state;
    if (state == MusicState::Silent) {
        transitionTo({}, MusicTiming::Immediate);
        return;
    }
    if (timing == MusicTiming::Auto) {
        // Callers without a MusicDirector (frontend, tools): menus and pauses
        // switch at once, everything else on the next measure.
        const bool immediate = state == MusicState::Menu || state == MusicState::Paused ||
                               previous == MusicState::Paused || previous == MusicState::Menu ||
                               previous == MusicState::Silent || state == MusicState::Results;
        timing = immediate ? MusicTiming::Immediate : MusicTiming::Measure;
    }
    transitionTo(segmentFor(state), timing);
    if (state == MusicState::Racing || state == MusicState::Start)
        m_startedSong = true;
}

void MusicEngine::triggerMotif() {
    if (!m_motif || !loadMotifStyle())
        return;
    // DMusicObject::PlayMotif plays the motif as a secondary segment from the
    // next beat (DMUS_SEGF_SECONDARY | GRID | BEAT; the beat is assumed to win
    // over the grid), with one repeat.
    m_motifDelay = m_playing.empty() ? 0 : OpenMM2_DmPerformance_samplesToBeat(m_music);
    m_motifRepeats = 1;
}

void MusicEngine::startMotif() {
    const MusicSong* s = currentSong();
    if (!s)
        return;
    const double seconds = OpenMM2_DmPerformance_playPattern(m_motif, m_playing.empty() ? nullptr : m_music,
                                                             m_motifStyle, s->motifName.c_str(),
                                                             s->motifBand.empty() ? nullptr : s->motifBand.c_str());
    if (seconds < 0) {
        log::warn("music: motif '{}' (band '{}') not found in {}", s->motifName, s->motifBand, m_motifStyleName);
        return;
    }
    // Let the last notes ring out before stopping the motif's performance.
    m_motifRemaining = static_cast<std::int64_t>((seconds + 1.5) * m_rate);
    // The repeat starts where the first play ends.
    if (m_motifRepeats > 0) {
        --m_motifRepeats;
        m_motifDelay = static_cast<std::int64_t>(seconds * m_rate);
    }
}

// Renders the motif performance into `out` (adds to it), starting a pending
// motif at the exact sample of the beat it was scheduled for.
void MusicEngine::renderMotif(float* out, int frames) {
    if (!m_motif || (m_motifRemaining <= 0 && m_motifDelay < 0))
        return;
    m_scratch.assign(static_cast<std::size_t>(frames) * 2, 0.0f);
    int done = 0;
    if (m_motifDelay >= 0 && m_motifDelay < frames) {
        const int before = static_cast<int>(m_motifDelay);
        if (before > 0 && m_motifRemaining > 0)
            DmPerformance_renderPcm(m_motif, m_scratch.data(), static_cast<std::size_t>(before) * 2,
                                    static_cast<DmRenderOptions>(DmRender_FLOAT | DmRender_STEREO));
        done = before;
        m_motifDelay = -1;
        startMotif();
        if (m_motifDelay >= 0) // a repeat is pending: count the rest of this block
            m_motifDelay -= frames - done;
    } else if (m_motifDelay >= 0) {
        m_motifDelay -= frames;
    }
    if (m_motifRemaining > 0 && done < frames) {
        DmPerformance_renderPcm(m_motif, m_scratch.data() + static_cast<std::size_t>(done) * 2,
                                static_cast<std::size_t>(frames - done) * 2,
                                static_cast<DmRenderOptions>(DmRender_FLOAT | DmRender_STEREO));
        m_motifRemaining -= frames - done;
        if (m_motifRemaining <= 0)
            DmPerformance_playSegment(m_motif, nullptr, DmTiming_INSTANT);
    }
    for (std::size_t i = 0; i < m_scratch.size(); ++i)
        out[i] += m_scratch[i];
}

void MusicEngine::setAmbience(std::string_view which) {
    const std::string segment = ambienceSegment(m_tables, which);
    if (str::iequals(segment, m_ambienceSegment) || !m_ambience)
        return;
    m_ambienceSegment = segment;
    if (segment.empty()) {
        DmPerformance_playSegment(m_ambience, nullptr, DmTiming_INSTANT);
        return;
    }
    if (DmSegment* sgt = m_library->segment(segment)) {
        DmPerformance_playSegment(m_ambience, sgt, DmTiming_INSTANT);
        DmSegment_release(sgt);
    }
}

bool MusicEngine::playSegment(std::string_view name) {
    DmSegment* sgt = m_library->segment(name);
    if (!sgt || !m_music)
        return false;
    DmPerformance_playSegment(m_music, sgt, DmTiming_INSTANT);
    DmSegment_release(sgt);
    m_playing = std::string(name);
    return true;
}

void MusicEngine::stopAll() {
    for (DmPerformance* p : {m_music, m_motif, m_ambience})
        if (p)
            DmPerformance_playSegment(p, nullptr, DmTiming_INSTANT);
    m_playing.clear();
    m_ambienceSegment.clear();
    m_motifRemaining = 0;
    m_motifDelay = -1;
    m_motifRepeats = 0;
    m_state = MusicState::Silent;
}

void MusicEngine::render(float* music, float* ambience, int frames) {
    const std::size_t n = static_cast<std::size_t>(frames) * 2;
    if (music) {
        std::fill(music, music + n, 0.0f);
        if (m_music)
            DmPerformance_renderPcm(m_music, music, n, static_cast<DmRenderOptions>(DmRender_FLOAT | DmRender_STEREO));
        renderMotif(music, frames);
    }
    if (ambience) {
        std::fill(ambience, ambience + n, 0.0f);
        if (m_ambience && !m_ambienceSegment.empty())
            DmPerformance_renderPcm(m_ambience, ambience, n,
                                    static_cast<DmRenderOptions>(DmRender_FLOAT | DmRender_STEREO));
    }
}

// --- MusicPlayer -------------------------------------------------------------

// Single-producer/single-consumer ring of interleaved stereo frames.
class MusicPlayer::Ring {
public:
    explicit Ring(std::size_t frames) : m_data(frames * 2), m_capacity(frames) {}

    std::size_t freeFrames() const {
        return m_capacity - (m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire));
    }

    void write(const float* src, std::size_t frames) {
        std::size_t w = m_write.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < frames; ++i, ++w) {
            const std::size_t slot = (w % m_capacity) * 2;
            m_data[slot] = src[i * 2];
            m_data[slot + 1] = src[i * 2 + 1];
        }
        m_write.store(w, std::memory_order_release);
    }

    // Returns the number of frames read; the rest of `dst` is left untouched.
    std::size_t read(float* dst, std::size_t frames) {
        std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t avail = m_write.load(std::memory_order_acquire) - r;
        const std::size_t n = std::min(avail, frames);
        for (std::size_t i = 0; i < n; ++i, ++r) {
            const std::size_t slot = (r % m_capacity) * 2;
            dst[i * 2] = m_data[slot];
            dst[i * 2 + 1] = m_data[slot + 1];
        }
        m_read.store(r, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> m_data;
    std::size_t m_capacity;
    std::atomic<std::size_t> m_write{0};
    std::atomic<std::size_t> m_read{0};
};

class MusicPlayer::Stream final : public StreamSource {
public:
    Stream(std::shared_ptr<Ring> ring, std::atomic<std::uint64_t>* underruns, bool countUnderruns)
        : m_ring(std::move(ring)), m_underruns(underruns), m_count(countUnderruns) {}

    void render(float* stereo, int frames) override {
        const std::size_t got = m_ring->read(stereo, static_cast<std::size_t>(frames));
        if (got < static_cast<std::size_t>(frames)) {
            std::fill(stereo + got * 2, stereo + static_cast<std::size_t>(frames) * 2, 0.0f);
            if (m_count && m_primed)
                m_underruns->fetch_add(1, std::memory_order_relaxed);
        }
        if (got > 0)
            m_primed = true;
    }

private:
    std::shared_ptr<Ring> m_ring;
    std::atomic<std::uint64_t>* m_underruns;
    bool m_count;
    bool m_primed = false; // don't count the start-up gap
};

MusicPlayer::MusicPlayer(const vfs::Vfs& vfs, int sampleRate) {
    m_tables = MusicTables::load(vfs);
    m_library = std::make_shared<MusicLibrary>(vfs);
    m_ok = m_library->ok();
    m_engine = std::make_unique<MusicEngine>(m_library, m_tables, sampleRate);
    m_musicRing = std::make_shared<Ring>(kBufferFrames);
    m_ambienceRing = std::make_shared<Ring>(kBufferFrames);
    m_musicStream = std::make_shared<Stream>(m_musicRing, &m_underruns, true);
    m_ambienceStream = std::make_shared<Stream>(m_ambienceRing, &m_underruns, false);
    m_thread = std::thread([this] { worker(); });
    m_loaderThread = std::thread([this] { loader(); });
}

MusicPlayer::~MusicPlayer() {
    m_quit = true;
    m_loadWake.notify_all();
    m_wake.notify_all();
    if (m_loaderThread.joinable())
        m_loaderThread.join();
    if (m_thread.joinable())
        m_thread.join();
}

void MusicPlayer::post(std::function<void(MusicEngine&)> cmd) {
    {
        std::lock_guard lock(m_mutex);
        m_commands.push_back(std::move(cmd));
    }
    m_wake.notify_all();
}

void MusicPlayer::loadThen(std::vector<std::string> names, std::function<void(MusicEngine&)> then) {
    {
        std::lock_guard lock(m_loadMutex);
        m_loads.push_back({std::move(names), std::move(then)});
    }
    m_loadWake.notify_all();
}

void MusicPlayer::loader() {
    while (true) {
        LoadRequest req;
        {
            std::unique_lock lock(m_loadMutex);
            m_loadWake.wait(lock, [this] { return m_quit || !m_loads.empty(); });
            if (m_quit)
                return;
            req = std::move(m_loads.front());
            m_loads.pop_front();
        }
        for (const auto& n : req.names) {
            if (m_quit)
                return;
            if (!n.empty())
                DmSegment_release(m_library->segment(n));
        }
        // Posting keeps the order of requests: a later request's command
        // cannot overtake an earlier one because loads run sequentially.
        post(std::move(req.then));
    }
}

void MusicPlayer::worker() {
    std::vector<float> music(static_cast<std::size_t>(kChunkFrames) * 2);
    std::vector<float> ambience(static_cast<std::size_t>(kChunkFrames) * 2);
    while (!m_quit) {
        std::deque<std::function<void(MusicEngine&)>> commands;
        {
            std::lock_guard lock(m_mutex);
            commands.swap(m_commands);
        }
        for (auto& cmd : commands)
            cmd(*m_engine);

        // The music stream paces rendering. Both streams are consumed at the
        // same rate in the game, but an unconsumed ambience stream must not
        // stall the music, so ambience that does not fit is dropped.
        if (m_musicRing->freeFrames() >= kChunkFrames) {
            m_engine->render(music.data(), ambience.data(), kChunkFrames);
            m_musicRing->write(music.data(), kChunkFrames);
            if (m_ambienceRing->freeFrames() >= kChunkFrames)
                m_ambienceRing->write(ambience.data(), kChunkFrames);
            continue;
        }
        // Buffers are full: wait about a quarter chunk, or until a command arrives.
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::milliseconds(3), [this] { return m_quit || !m_commands.empty(); });
    }
}

std::vector<std::string> MusicPlayer::songSegments(int song, bool cruise) const {
    const auto& songs = cruise ? m_tables.cruise : m_tables.race;
    if (songs.empty() || song < 0)
        return {};
    const MusicSong& s = songs[static_cast<std::size_t>(song) % songs.size()];
    return {s.start, s.ret, s.idle, s.idleCops, s.cops, s.pause, s.results, s.motifName};
}

void MusicPlayer::playMenu() {
    loadThen({m_tables.menu}, [](MusicEngine& e) { e.setState(MusicState::Menu); });
}

void MusicPlayer::startRace(int song, bool cruise, bool play) {
    // Pick a random song here so the right segments are preloaded
    // (mmSingleRaceMusicData / mmSingleRoamMusicData::LoadMusic pick a table
    // row uniformly at random).
    const auto& songs = cruise ? m_tables.cruise : m_tables.race;
    if (song < 0 && !songs.empty()) {
        static std::mt19937 rng{std::random_device{}()};
        song = static_cast<int>(rng() % songs.size());
    }
    loadThen(songSegments(song, cruise), [song, cruise, play](MusicEngine& e) {
        e.selectSong(song, cruise);
        if (play)
            e.setState(MusicState::Racing);
        else
            e.setState(MusicState::Silent);
    });
}

// Every command goes through the loader queue, even without segments to load,
// so commands reach the engine in the order they were issued (a setState()
// right after startRace() must not overtake the race start).
void MusicPlayer::setState(MusicState state, MusicTiming timing) {
    // The current song's segments were preloaded by startRace.
    loadThen({}, [state, timing](MusicEngine& e) { e.setState(state, timing); });
}

void MusicPlayer::triggerBigAir() {
    loadThen({}, [](MusicEngine& e) { e.triggerMotif(); });
}

void MusicPlayer::setAmbience(std::string_view which) {
    loadThen({ambienceSegment(m_tables, which)}, [w = std::string(which)](MusicEngine& e) { e.setAmbience(w); });
}

void MusicPlayer::playSegment(std::string_view name) {
    loadThen({std::string(name)}, [n = std::string(name)](MusicEngine& e) { e.playSegment(n); });
}

void MusicPlayer::stop() {
    loadThen({}, [](MusicEngine& e) { e.stopAll(); });
}

} // namespace mm2::audio
