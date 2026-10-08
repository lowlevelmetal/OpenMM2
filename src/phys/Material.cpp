#include "phys/Material.h"

#include "core/StringUtil.h"
#include "data/CNumbers.h"

#include <cstdint>
#include <format>

namespace mm2::phys {
namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

class Reader {
public:
    explicit Reader(std::string_view t) : m_text(t) {}

    // Next whitespace-delimited token; '{', '}' and ':' are separate tokens.
    std::string_view next() {
        while (m_pos < m_text.size() && isSpace(m_text[m_pos])) {
            if (m_text[m_pos] == '\n')
                ++m_line;
            ++m_pos;
        }
        if (m_pos >= m_text.size())
            return {};
        const std::size_t start = m_pos;
        const char c = m_text[m_pos];
        if (c == '{' || c == '}' || c == ':') {
            ++m_pos;
            return m_text.substr(start, 1);
        }
        while (m_pos < m_text.size() && !isSpace(m_text[m_pos]) && m_text[m_pos] != '{' &&
               m_text[m_pos] != '}' && m_text[m_pos] != ':')
            ++m_pos;
        return m_text.substr(start, m_pos - start);
    }

    // Rest of the current line (for multi-value keys).
    std::string_view restOfLine() {
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && m_text[m_pos] != '\n' && m_text[m_pos] != '}')
            ++m_pos;
        return m_text.substr(start, m_pos - start);
    }

    int line() const { return m_line; }

private:
    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
};


} // namespace

Material lvlMaterialDefault() {
    Material m;
    m.name = "default";
    m.elasticity = 0.5f;
    m.friction = 1.0f;
    m.effect = "none";
    m.sound = -1;
    m.drag = 0.0f;
    m.width = 1.0f;
    m.height = 0.0f;
    m.depth = 0.0f;
    m.ptxIndex[0] = m.ptxIndex[1] = -1;
    m.ptxThreshold[0] = 0.25f;
    m.ptxThreshold[1] = 0.5f;
    return m;
}

std::optional<std::vector<Material>> parseMaterials(std::string_view text, std::string* error) {
    std::vector<Material> out;
    Reader r(text);
    auto fail = [&](std::string msg) -> std::optional<std::vector<Material>> {
        if (error)
            *error = std::format("line {}: {}", r.line(), msg);
        return std::nullopt;
    };
    while (true) {
        std::string_view tok = r.next();
        if (tok.empty())
            break;
        if (tok != "mtl")
            continue; // other sections of a .bnd file
        Material m = lvlMaterialDefault();
        m.name = std::string(r.next());
        if (r.next() != "{")
            return fail("expected '{' after material name");
        while (true) {
            std::string_view key = r.next();
            if (key.empty())
                return fail("unterminated mtl block");
            if (key == "}")
                break;
            if (r.next() != ":")
                return fail(std::format("expected ':' after '{}'", key));
            const auto values = str::split(str::trim(r.restOfLine()), ' ');
            std::vector<std::string_view> vals;
            for (auto v : values)
                if (!str::trim(v).empty())
                    vals.push_back(str::trim(v));
            auto val = [&](std::size_t i) { return i < vals.size() ? vals[i] : std::string_view{}; };
            // lvlMaterial::Load reads the numbers with datAsciiTokenizer's
            // GetFloat / GetInt (a token not starting like a number is 0,
            // else atof / atoi of its prefix). It reads the keys in the
            // retail order and stops after sound or depth at a '}'; any
            // order is OpenMM2 leniency.
            if (key == "elasticity")
                m.elasticity = data::datTokenFloat(val(0));
            else if (key == "friction")
                m.friction = data::datTokenFloat(val(0));
            else if (key == "effect")
                m.effect = std::string(val(0));
            else if (key == "sound")
                // A plain token: "none" (its first four letters, any case) is 0,
                // anything else atoi.
                m.sound =
                    str::istartsWith(val(0), "none") ? 0 : static_cast<std::int16_t>(data::cAtoi(val(0)));
            else if (key == "drag")
                m.drag = data::datTokenFloat(val(0));
            else if (key == "width")
                m.width = data::datTokenFloat(val(0));
            else if (key == "height")
                m.height = data::datTokenFloat(val(0));
            else if (key == "depth")
                m.depth = data::datTokenFloat(val(0));
            else if (key == "ptxindex") {
                m.ptxIndex[0] = static_cast<std::int16_t>(data::datTokenInt(val(0)));
                m.ptxIndex[1] = static_cast<std::int16_t>(data::datTokenInt(val(1)));
            } else if (key == "ptxthreshold") {
                m.ptxThreshold[0] = data::datTokenFloat(val(0));
                m.ptxThreshold[1] = data::datTokenFloat(val(1));
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}

MaterialTable::MaterialTable() {
    m_materials.push_back(lvlMaterialDefault());
}

void MaterialTable::add(const Material& m) {
    if (find(m.name) < 0)
        m_materials.push_back(m);
}

void MaterialTable::add(const std::vector<Material>& ms) {
    for (const auto& m : ms)
        add(m);
}

int MaterialTable::find(std::string_view name) const {
    for (std::size_t i = 0; i < m_materials.size(); ++i)
        if (str::iequals(m_materials[i].name, name))
            return static_cast<int>(i);
    return -1;
}

int MaterialTable::resolve(std::string_view name) const {
    const int i = find(name);
    return i >= 0 ? i : 0;
}

const Material& MaterialTable::operator[](int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_materials.size())
        return m_materials[0];
    return m_materials[static_cast<std::size_t>(index)];
}

} // namespace mm2::phys
