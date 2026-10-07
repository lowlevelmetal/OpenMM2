#include "audio/game/AudioTables.h"

#include "core/StringUtil.h"
#include "data/TextTables.h"

#include <cmath>
#include <format>

namespace mm2::audio::game {
namespace {

using Row = std::vector<std::string>;

// Splits CSV text into trimmed cells, dropping trailing empty cells and
// lines that are empty after that.
std::vector<Row> rows(std::string_view text) {
    std::vector<Row> out;
    for (auto line : data::splitLines(text)) {
        Row r;
        for (auto cell : str::split(line, ','))
            r.emplace_back(str::trim(cell));
        while (!r.empty() && r.back().empty())
            r.pop_back();
        if (!r.empty())
            out.push_back(std::move(r));
    }
    return out;
}

float num(const Row& r, std::size_t i, float fallback = 0.0f) {
    if (i >= r.size())
        return fallback;
    auto d = str::parseDouble(r[i]);
    return d ? static_cast<float>(*d) : fallback;
}

int inum(const Row& r, std::size_t i, int fallback = 0) {
    if (i >= r.size())
        return fallback;
    if (auto v = str::parseInt(r[i]))
        return static_cast<int>(*v);
    if (auto d = str::parseDouble(r[i]))
        return static_cast<int>(*d);
    return fallback;
}

bool isNumber(const Row& r, std::size_t i) { return i < r.size() && str::parseDouble(r[i]).has_value(); }

bool startsWith(const Row& r, std::string_view prefix) { return !r.empty() && str::istartsWith(r[0], prefix); }

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

} // namespace

float ageVolumeToGain(float v) {
    if (v <= 0.0f)
        return 0.0f;
    if (v >= 1.0f)
        return 1.0f;
    // (v - 1) * 10000 hundredths of a decibel -> amplitude.
    return std::pow(10.0f, (v - 1.0f) * 5.0f);
}

std::optional<std::string> readText(const vfs::Vfs& vfs, std::string_view path) {
    auto bytes = vfs.readAll(path);
    if (!bytes)
        return std::nullopt;
    return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

// --- Cars ---------------------------------------------------------------------

std::optional<CarAudioDef> parseCarAudio(std::string_view text, std::string* error) {
    const auto r = rows(text);
    CarAudioDef def;
    std::size_t i = 0;
    // "Horn wave name,Horn volume,flags,Num Engine Samples,clutch wave name,clutch volume"
    while (i < r.size() && !startsWith(r[i], "Horn wave"))
        ++i;
    if (i + 1 >= r.size()) {
        setError(error, "no horn header");
        return std::nullopt;
    }
    const Row& h = r[++i];
    def.horn = h.size() > 0 ? h[0] : std::string();
    def.hornVolume = num(h, 1, 0.95f);
    def.flags = static_cast<unsigned>(inum(h, 2));
    const int count = inum(h, 3);
    def.clutch = h.size() > 4 ? h[4] : std::string();
    def.clutchVolume = num(h, 5, 0.9f);
    ++i;
    while (i < r.size() && !startsWith(r[i], "Engine wave"))
        ++i;
    ++i;
    // The engine rows follow; "Num Engine Samples" is not always accurate
    // (opponent files list fewer rows than they claim), so read what is there.
    for (; i < r.size() && r[i].size() >= 11 && isNumber(r[i], 1); ++i) {
        const Row& e = r[i];
        EngineSampleDef s;
        s.wave = e[0];
        s.minVolume = num(e, 1);
        s.maxVolume = num(e, 2);
        s.fadeInStartRpm = num(e, 3);
        s.fadeInEndRpm = num(e, 4);
        s.fadeOutStartRpm = num(e, 5);
        s.fadeOutEndRpm = num(e, 6);
        s.minPitch = num(e, 7, 1);
        s.maxPitch = num(e, 8, 1);
        s.pitchStartRpm = num(e, 9);
        s.pitchEndRpm = num(e, 10, 1);
        def.engine.push_back(std::move(s));
    }
    (void)count;
    if (def.engine.empty()) {
        setError(error, "no engine samples");
        return std::nullopt;
    }
    return def;
}

// --- Impacts ------------------------------------------------------------------

const BangerSoundDef* ImpactTable::find(int id) const {
    for (const auto& b : bangers)
        if (b.id == id)
            return &b;
    return nullptr;
}

std::optional<ImpactTable> parseImpactTable(std::string_view text, std::string* error) {
    const auto r = rows(text);
    ImpactTable t;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (!startsWith(r[i], "Banger name") || i + 1 >= r.size())
            continue;
        const Row& h = r[++i];
        if (h.empty() || str::iequals(h[0], "ENDOFDATA"))
            break;
        BangerSoundDef b;
        b.name = h[0];
        const int count = inum(h, 1);
        b.id = inum(h, 2);
        ++i; // "sample name,min volume,..." header
        for (int k = 0; k < count && i + 1 < r.size(); ++k) {
            const Row& s = r[++i];
            if (!isNumber(s, 1))
                break;
            ImpactSampleDef d;
            d.wave = s[0];
            d.minVolume = num(s, 1);
            d.maxVolume = num(s, 2);
            d.minForce = num(s, 3);
            d.maxForce = num(s, 4);
            d.frequency = num(s, 5, 1);
            b.samples.push_back(std::move(d));
        }
        t.bangers.push_back(std::move(b));
    }
    if (t.bangers.empty()) {
        setError(error, "no banger entries");
        return std::nullopt;
    }
    return t;
}

// --- Surfaces -----------------------------------------------------------------

bool SurfaceSoundDef::hasSurfaceSound() const { return !wave.empty() && !str::iequals(wave, "NOSOUND"); }

const SurfaceSoundDef* SurfaceTable::at(int index) const {
    if (surfaces.empty())
        return nullptr;
    if (index < 0 || static_cast<std::size_t>(index) >= surfaces.size())
        index = 0;
    return &surfaces[static_cast<std::size_t>(index)];
}

std::optional<SurfaceTable> parseSurfaceTable(std::string_view text, std::string* error) {
    const auto r = rows(text);
    SurfaceTable t;
    std::size_t i = 0;
    while (i < r.size() && !startsWith(r[i], "Tunnel sound"))
        ++i;
    if (i + 1 < r.size())
        t.tunnelIndex = inum(r[++i], 0);
    for (; i < r.size(); ++i) {
        if (!startsWith(r[i], "surface wave") || i + 1 >= r.size())
            continue;
        t.ice = r[i].size() > 1 && str::iequals(r[i][1], "min surface volume");
        const Row& s = r[++i];
        if (s.empty() || str::iequals(s[0], "ENDOFDATA"))
            break;
        SurfaceSoundDef d;
        d.wave = s[0];
        int skids = 0;
        if (t.ice) {
            // wave, min vol, max vol, vol divisor, min pitch, max pitch,
            // pitch divisor, min skid vol, max skid vol, skid vol divisor,
            // num skid samples, for tunnels
            d.minVolume = num(s, 1);
            d.maxVolume = num(s, 2);
            d.volumeDivisor = num(s, 3);
            d.minPitch = num(s, 4, 1);
            d.maxPitch = num(s, 5, 1);
            d.pitchDivisor = num(s, 6);
            d.minSkidVolume = num(s, 7);
            d.maxSkidVolume = num(s, 8);
            d.skidVolumeDivisor = num(s, 9);
            skids = inum(s, 10);
            d.forTunnels = inum(s, 11) != 0;
        } else {
            // wave, max speed, min vol, max vol, min pitch, max pitch, min skid
            // vol, max skid vol, num skid samples
            d.maxSpeed = num(s, 1, 125);
            d.minVolume = num(s, 2);
            d.maxVolume = num(s, 3);
            d.minPitch = num(s, 4, 1);
            d.maxPitch = num(s, 5, 1);
            d.minSkidVolume = num(s, 6);
            d.maxSkidVolume = num(s, 7);
            skids = inum(s, 8);
        }
        if (i + 1 < r.size() && startsWith(r[i + 1], "skid wave"))
            ++i;
        for (int k = 0; k < skids && i + 1 < r.size() && isNumber(r[i + 1], 1); ++k) {
            const Row& sk = r[++i];
            d.skids.push_back({sk[0], num(sk, 1), num(sk, 2, 1)});
        }
        t.surfaces.push_back(std::move(d));
    }
    if (t.surfaces.empty()) {
        setError(error, "no surface entries");
        return std::nullopt;
    }
    return t;
}

// --- Sirens -------------------------------------------------------------------

std::optional<SirenTable> parseSirenTable(std::string_view text, std::string* error) {
    const auto r = rows(text);
    SirenTable t;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (startsWith(r[i], "Explosion sample") && i + 1 < r.size()) {
            t.explosion = r[i + 1][0];
            t.explosionVolume = num(r[i + 1], 1, 0.95f);
            ++i;
        } else if (startsWith(r[i], "Sample name") && i + 1 < r.size()) {
            SirenSampleDef s;
            ++i;
            s.wave = r[i][0];
            s.volume = num(r[i], 1, 0.95f);
            while (i + 2 < r.size() && startsWith(r[i + 1], "play time")) {
                s.steps.push_back({num(r[i + 2], 0, 1), inum(r[i + 2], 1)});
                i += 2;
            }
            t.samples.push_back(std::move(s));
        }
    }
    if (t.samples.empty()) {
        setError(error, "no siren samples");
        return std::nullopt;
    }
    return t;
}

// --- Small tables -------------------------------------------------------------

std::optional<SuspensionDef> parseSuspension(std::string_view text) {
    const auto r = rows(text);
    if (r.size() < 2 || !isNumber(r[1], 1))
        return std::nullopt;
    SuspensionDef d;
    d.wave = r[1][0];
    d.minVelocity = num(r[1], 1, 2);
    d.maxVelocity = num(r[1], 2, 3);
    d.minVolume = num(r[1], 3, 0.85f);
    d.maxVolume = num(r[1], 4, 0.9f);
    d.volumeDivisor = num(r[1], 5, 3);
    return d;
}

std::optional<TireWobbleDef> parseTireWobble(std::string_view text) {
    const auto r = rows(text);
    if (r.size() < 2 || !isNumber(r[1], 1))
        return std::nullopt;
    TireWobbleDef d;
    d.wave = r[1][0];
    d.minVolume = num(r[1], 1, 0.97f);
    d.maxVolume = num(r[1], 2, 1);
    d.minPitch = num(r[1], 3, 0.75f);
    d.maxPitch = num(r[1], 4, 1.5f);
    d.pitchDivisor = num(r[1], 5, 15);
    return d;
}

std::optional<SemiDef> parseSemiData(std::string_view text) {
    const auto r = rows(text);
    if (r.size() < 2 || r[1].size() < 2)
        return std::nullopt;
    SemiDef d;
    d.reverse = r[1][0];
    d.airBlow = r[1][1];
    d.reverseVolume = num(r[1], 2, 0.87f);
    d.airBlowVolume = num(r[1], 3, 0.9f);
    return d;
}

bool VehicleTypes::isFreight(std::string_view car) const {
    for (const auto& c : freight)
        if (str::iequals(c, car))
            return true;
    return false;
}

bool VehicleTypes::isPolice(std::string_view car) const {
    for (const auto& c : police)
        if (str::iequals(c, car))
            return true;
    return false;
}

VehicleTypes parseVehicleTypes(std::string_view text) {
    VehicleTypes t;
    const auto r = rows(text);
    for (std::size_t i = 0; i + 1 < r.size(); ++i) {
        std::vector<std::string>* list = nullptr;
        if (startsWith(r[i], "Semi or bus"))
            list = &t.freight;
        else if (startsWith(r[i], "Police"))
            list = &t.police;
        else if (startsWith(r[i], "Always nitro")) {
            t.alwaysNitro = str::parseBool(r[i + 1][0]).value_or(false);
            continue;
        }
        if (!list)
            continue;
        for (const auto& c : r[i + 1]) {
            if (str::iequals(c, "ENDOFDATA"))
                break;
            list->push_back(str::lower(c));
        }
    }
    return t;
}

// --- Ambient traffic ------------------------------------------------------------

std::optional<AmbientEngineDef> parseAmbientEngine(std::string_view text) {
    const auto r = rows(text);
    if (r.size() < 2)
        return std::nullopt;
    AmbientEngineDef d;
    d.wave = r[1][0];
    d.volume = num(r[1], 1, 0.97f);
    for (std::size_t i = 2; i < r.size(); ++i) {
        if (r[i].size() < 4 || !isNumber(r[i], 0))
            continue;
        d.bands.push_back({num(r[i], 0), num(r[i], 1), num(r[i], 2, 1), num(r[i], 3, 1)});
    }
    if (d.bands.empty())
        return std::nullopt;
    return d;
}

std::optional<HornDef> parseHorn(std::string_view text) {
    const auto r = rows(text);
    if (r.size() < 2)
        return std::nullopt;
    HornDef d;
    d.wave = r[1][0];
    d.volume = num(r[1], 1, 0.97f);
    d.pitch = num(r[1], 2, 1);
    d.stuckImpactForce = num(r[1], 3, 5500);
    for (std::size_t i = 2; i < r.size(); ++i) {
        if (startsWith(r[i], "horn play duration")) {
            d.patterns.emplace_back();
            continue;
        }
        if (!d.patterns.empty() && isNumber(r[i], 0))
            d.patterns.back().beeps.emplace_back(num(r[i], 0), num(r[i], 1));
    }
    return d;
}

// --- City ambience ----------------------------------------------------------------

std::optional<AmbientSoundSet> parseAmbientSoundSet(std::string_view name, std::string_view text, std::string* error) {
    const auto r = rows(text);
    AmbientSoundSet s;
    s.name = str::lower(name);
    std::size_t i = 0;
    if (r.size() < 2 || !startsWith(r[0], "Min distance")) {
        setError(error, "missing header");
        return std::nullopt;
    }
    s.minDistance = num(r[1], 0);
    s.maxDistance = num(r[1], 1, 100);
    s.priority = inum(r[1], 2, 12);
    s.audibleArea = inum(r[1], 3);
    for (i = 2; i < r.size(); ++i) {
        if (startsWith(r[i], "sample name"))
            continue;
        if (startsWith(r[i], "VECTORPOINTS"))
            break;
        const Row& e = r[i];
        if (e.size() < 3 || !isNumber(e, 1))
            continue;
        AmbientSampleDef d;
        d.wave = e[0];
        d.volume = num(e, 1, 1);
        d.type = static_cast<AmbientSampleType>(std::clamp(inum(e, 2), 0, 3));
        d.intervalLow = num(e, 3);
        d.intervalHigh = num(e, 4);
        d.active = inum(e, 5, 1) != 0;
        d.minSpeed = num(e, 6);
        d.maxSpeed = num(e, 7, 999999);
        d.doppler = inum(e, 8) != 0;
        s.samples.push_back(std::move(d));
    }
    for (; i < r.size(); ++i) {
        if (r[i].size() >= 3 && isNumber(r[i], 0) && isNumber(r[i], 2))
            s.points.push_back({num(r[i], 0), num(r[i], 1), num(r[i], 2)});
    }
    return s;
}

std::vector<std::string> parseAmbientContainer(std::string_view text) {
    std::vector<std::string> names;
    const auto r = rows(text);
    for (std::size_t i = 1; i < r.size(); ++i)
        names.push_back(str::lower(r[i][0]));
    return names;
}

// --- Creature voices --------------------------------------------------------------

std::optional<CreatureVoiceDef> parseCreatureVoice(std::string_view text) {
    const auto r = rows(text);
    CreatureVoiceDef d;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (startsWith(r[i], "Min speed") && i + 1 < r.size()) {
            VoiceSpeedTrigger t;
            const Row& v = r[++i];
            t.minSpeed = num(v, 0);
            t.maxSpeed = num(v, 1);
            t.minTimeInRange = num(v, 2);
            t.maxTimeOutOfRange = num(v, 3);
            if (i + 1 < r.size() && startsWith(r[i + 1], "sample name"))
                ++i;
            while (i + 1 < r.size() && r[i + 1].size() >= 2 && isNumber(r[i + 1], 1) && !isNumber(r[i + 1], 0)) {
                t.lines.push_back({r[i + 1][0], num(r[i + 1], 1, 0.98f), 0});
                ++i;
            }
            d.triggers.push_back(std::move(t));
        } else if (startsWith(r[i], "min impact force") && i + 1 < r.size()) {
            d.minImpactForce = num(r[++i], 0);
            if (i + 1 < r.size() && startsWith(r[i + 1], "sample name"))
                ++i;
            while (i + 1 < r.size() && r[i + 1].size() >= 2 && isNumber(r[i + 1], 1) && !isNumber(r[i + 1], 0)) {
                d.impactLines.push_back({r[i + 1][0], num(r[i + 1], 1, 0.98f), num(r[i + 1], 2)});
                ++i;
            }
        }
    }
    if (d.triggers.empty() && d.impactLines.empty())
        return std::nullopt;
    return d;
}

// --- Announcer --------------------------------------------------------------------

const std::vector<SpeechLineSet>* SpeechTable::find(std::string_view event) const {
    for (const auto& [name, sets] : events)
        if (str::iequals(name, event))
            return &sets;
    return nullptr;
}

std::optional<SpeechTable> parseSpeechTable(std::string_view text) {
    const auto r = rows(text);
    SpeechTable t;
    for (const auto& row : r) {
        if (str::istartsWith(row[0], "Name prefix"))
            continue;
        if (str::iendsWith(row[0], "header")) {
            std::string event(str::trim(row[0].substr(0, row[0].size() - 6)));
            t.events.emplace_back(str::upper(event), std::vector<SpeechLineSet>{});
            continue;
        }
        if (t.events.empty() || row.size() < 2)
            continue;
        SpeechLineSet s;
        std::string prefix = row[0];
        // Cops & Robbers tables give "AL1\AL1ROBROB": keep the file part.
        if (auto slash = prefix.find_last_of("\\/"); slash != std::string::npos)
            prefix = prefix.substr(slash + 1);
        s.prefix = str::lower(str::trim(prefix));
        s.end = inum(row, 1, 1);
        s.add = inum(row, 2, 0);
        t.events.back().second.push_back(std::move(s));
    }
    if (t.events.empty())
        return std::nullopt;
    return t;
}

std::optional<AnnouncerList> parseAnnouncerList(std::string_view text) {
    const auto r = rows(text);
    AnnouncerList a;
    for (std::size_t i = 0; i + 1 < r.size(); ++i) {
        if (startsWith(r[i], "Num announcers"))
            a.count = inum(r[i + 1], 0);
        else if (startsWith(r[i], "prefix"))
            a.prefix = str::lower(r[i + 1][0]);
    }
    if (a.count <= 0 || a.prefix.empty())
        return std::nullopt;
    return a;
}

} // namespace mm2::audio::game
