#include "city/Environment.h"

#include "city/Reader.h"
#include "core/StringUtil.h"
#include "data/CNumbers.h"
#include "data/DatFile.h"
#include "data/TextTables.h"

#include <cstdint>
#include <format>

namespace mm2::city {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        const std::size_t s = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            ++i;
        if (i > s)
            out.push_back(line.substr(s, i - s));
    }
    return out;
}

// A number as MM2's loaders read one (atof / sscanf "%f"): the numeric prefix
// of the word, nothing when it has none.
std::optional<float> num(std::string_view s) {
    if (const auto v = data::atofPrefix(s))
        return static_cast<float>(*v);
    return std::nullopt;
}

} // namespace

std::optional<LightingDef> parseLighting(std::string_view text, std::string* error) {
    auto f = data::parseDat(text, error);
    if (!f)
        return std::nullopt;
    const auto* top = f->top();
    if (!top) {
        setError(error, "no lighting block");
        return std::nullopt;
    }
    LightingDef l;
    l.name = top->name;
    top->read("KeyHeading", l.keyHeading);
    top->read("KeyPitch", l.keyPitch);
    top->read("KeyColor", l.keyColor);
    top->read("Fill1Heading", l.fill1Heading);
    top->read("Fill1Pitch", l.fill1Pitch);
    top->read("Fill1Color", l.fill1Color);
    top->read("Fill2Heading", l.fill2Heading);
    top->read("Fill2Pitch", l.fill2Pitch);
    top->read("Fill2Color", l.fill2Color);
    if (const auto* amb = top->child("Ambient"); amb && !amb->numbers.empty())
        l.ambient = static_cast<std::uint32_t>(static_cast<std::int64_t>(amb->numbers[0]));
    return l;
}

std::optional<std::vector<FogDef>> parseFogTable(std::string_view text, std::string* error) {
    const auto t = data::CsvTable::parse(text, true);
    std::vector<FogDef> out;
    for (std::size_t r = 0; r < t.rows().size(); ++r) {
        const auto& row = t.rows()[r];
        if (row.size() < 5) {
            setError(error, std::format("row {}: expected 5 columns", r + 2));
            return std::nullopt;
        }
        FogDef fog;
        fog.r = static_cast<std::uint8_t>(data::cAtoi(row[0]));
        fog.g = static_cast<std::uint8_t>(data::cAtoi(row[1]));
        fog.b = static_cast<std::uint8_t>(data::cAtoi(row[2]));
        // lvlSky::AutoInit reads the fog distances with atoi too.
        fog.start = static_cast<float>(data::cAtoi(row[3]));
        fog.end = static_cast<float>(data::cAtoi(row[4]));
        if (row.size() > 5)
            fog.description = row[5];
        out.push_back(std::move(fog));
    }
    return out;
}

std::optional<SkyDef> parseSky(std::string_view text, std::string* error) {
    for (auto line : data::splitLines(text)) {
        const auto w = words(str::trim(line));
        if (w.empty())
            continue;
        SkyDef s{std::string(w[0]), {}};
        for (std::size_t i = 1; i < w.size(); ++i)
            if (auto v = num(w[i]))
                s.params.push_back(*v);
        return s;
    }
    setError(error, "empty sky file");
    return std::nullopt;
}

std::optional<WaterDef> parseWater(std::string_view text, std::string* error) {
    WaterDef w;
    bool first = true;
    for (auto line : data::splitLines(text)) {
        line = str::trim(line);
        if (line.empty())
            continue;
        auto v = num(line);
        if (!v) {
            setError(error, std::format("bad water line '{}'", line));
            return std::nullopt;
        }
        if (first)
            w.height = *v;
        else
            w.rooms.push_back(static_cast<int>(*v));
        first = false;
    }
    if (first) {
        setError(error, "empty water file");
        return std::nullopt;
    }
    return w;
}

std::optional<MapExtent> parseExtent(std::string_view text, std::string* error) {
    std::vector<float> v;
    for (auto line : data::splitLines(text))
        for (auto w : words(str::trim(line)))
            if (auto f = num(w))
                v.push_back(*f);
    if (v.size() < 4) {
        setError(error, "expected 4 numbers");
        return std::nullopt;
    }
    return MapExtent{v[0], v[1], v[2], v[3]};
}

std::vector<ResetPoint> parseResetPoints(std::string_view text) {
    std::vector<ResetPoint> out;
    for (auto line : data::splitLines(text)) {
        std::string comment;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            comment = std::string(str::trim(line.substr(hash + 1)));
            line = line.substr(0, hash);
        }
        const auto w = words(str::trim(line));
        if (w.size() < 3)
            continue;
        auto x = num(w[0]), y = num(w[1]), z = num(w[2]);
        if (x && y && z)
            out.push_back({{*x, *y, *z}, std::move(comment)});
    }
    return out;
}

std::optional<std::vector<std::uint32_t>> parseLightMap(std::span<const std::byte> data, std::string* error) {
    detail::Reader r(data);
    if (!r.magic("LMP0")) {
        setError(error, "not a light map (missing LMP0)");
        return std::nullopt;
    }
    const std::uint32_t n = r.u32();
    if (!r.ok() || n != r.remaining() / 4 || r.remaining() % 4) {
        setError(error, std::format("light map count {} does not match size", n));
        return std::nullopt;
    }
    std::vector<std::uint32_t> out(n);
    for (auto& c : out)
        c = r.u32();
    return out;
}

std::optional<std::vector<PhysMaterial>> parseMaterialLibrary(std::string_view text, std::string* error) {
    std::vector<PhysMaterial> out;
    PhysMaterial* cur = nullptr;
    int lineNo = 0;
    for (auto raw : data::splitLines(text)) {
        ++lineNo;
        const auto line = str::trim(raw);
        if (line.empty())
            continue;
        const auto w = words(line);
        if (w.size() >= 2 && w[0] == "mtl") {
            out.push_back(PhysMaterial{});
            cur = &out.back();
            cur->name = std::string(w[1]);
            continue;
        }
        if (line == "}") {
            cur = nullptr;
            continue;
        }
        if (!cur || w.empty() || !w[0].ends_with(':')) {
            setError(error, std::format("line {}: unexpected '{}'", lineNo, line));
            return std::nullopt;
        }
        const auto key = w[0].substr(0, w[0].size() - 1);
        // lvlMaterial::Load: datAsciiTokenizer's GetFloat / GetInt (a token
        // not starting like a number is 0, else atof / atoi of its prefix);
        // the sound is a plain token, "none" (its first four letters, any case) 0, else
        // atoi. MM2 reads the keys in the retail order.
        auto token = [&](std::size_t i) { return i < w.size() ? w[i] : std::string_view{}; };
        auto f = [&](std::size_t i) { return data::datTokenFloat(token(i)); };
        auto n = [&](std::size_t i) { return static_cast<std::int16_t>(data::datTokenInt(token(i))); };
        if (key == "elasticity")
            cur->elasticity = f(1);
        else if (key == "friction")
            cur->friction = f(1);
        else if (key == "drag")
            cur->drag = f(1);
        else if (key == "width")
            cur->width = f(1);
        else if (key == "height")
            cur->height = f(1);
        else if (key == "depth")
            cur->depth = f(1);
        else if (key == "effect")
            cur->effect = w.size() > 1 ? std::string(w[1]) : std::string();
        else if (key == "sound")
            cur->sound =
                str::istartsWith(token(1), "none") ? 0 : static_cast<std::int16_t>(data::cAtoi(token(1)));
        else if (key == "ptxindex")
            cur->ptxIndex = {n(1), n(2)};
        else if (key == "ptxthreshold")
            cur->ptxThreshold = {f(1), f(2)};
    }
    return out;
}

std::vector<TextureMaterial> parseTextureMaterials(std::string_view text) {
    std::vector<TextureMaterial> out;
    const auto t = data::CsvTable::parse(text, true);
    for (const auto& row : t.rows())
        if (row.size() >= 2 && !row[0].empty())
            out.push_back({row[0], row[1]});
    return out;
}

} // namespace mm2::city
