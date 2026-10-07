#include "ui/Font.h"

#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace mm2::ui {
namespace {

const stbtt_fontinfo* infoOf(const FontFile& f) { return static_cast<const stbtt_fontinfo*>(f.info()); }

// Font file names, original first, then the bundled substitute.
struct FaceFiles {
    std::vector<const char*> system;
    const char* bundled;
};

FaceFiles filesFor(std::string_view face, bool bold) {
    const std::string f = str::lower(face);
    if (f.find("gill") != std::string::npos) {
        if (bold)
            return {{"GILB____.TTF", "GillSansMT-Bold.ttf", "Gill Sans MT Bold.ttf"}, "GilliusADFNo2-Bold.otf"};
        return {{"GIL_____.TTF", "GillSansMT.ttf", "Gill Sans MT.ttf"}, "GilliusADFNo2-Regular.otf"};
    }
    if (bold)
        return {{"arialbd.ttf", "Arial_Bold.ttf", "Arial Bold.ttf", "LiberationSans-Bold.ttf"}, "LiberationSans-Bold.ttf"};
    return {{"arial.ttf", "Arial.ttf", "LiberationSans-Regular.ttf"}, "LiberationSans-Regular.ttf"};
}

// Lower-cased file name -> path for every font in the system/user font folders.
const std::unordered_map<std::string, std::filesystem::path>& systemFontIndex() {
    static std::once_flag once;
    static std::unordered_map<std::string, std::filesystem::path> index;
    std::call_once(once, [] {
        std::vector<std::filesystem::path> roots;
#ifdef _WIN32
        if (const char* windir = std::getenv("WINDIR"))
            roots.emplace_back(std::filesystem::path(windir) / "Fonts");
        if (const char* local = std::getenv("LOCALAPPDATA"))
            roots.emplace_back(std::filesystem::path(local) / "Microsoft" / "Windows" / "Fonts");
#else
        roots = {"/usr/share/fonts", "/usr/local/share/fonts"};
        if (const char* home = std::getenv("HOME")) {
            roots.emplace_back(std::filesystem::path(home) / ".local/share/fonts");
            roots.emplace_back(std::filesystem::path(home) / ".fonts");
        }
#endif
        std::error_code ec;
        for (const auto& root : roots) {
            std::filesystem::recursive_directory_iterator it(
                root, std::filesystem::directory_options::skip_permission_denied, ec),
                end;
            for (int n = 0; !ec && it != end && n < 50000; it.increment(ec), ++n) {
                if (it.depth() > 4)
                    it.disable_recursion_pending();
                const std::string name = str::lower(str::fromPath(it->path().filename()));
                if (name.ends_with(".ttf") || name.ends_with(".otf"))
                    index.try_emplace(name, it->path());
            }
        }
    });
    return index;
}

} // namespace

std::optional<FontSpec> FontSpec::parse(std::string_view text) {
    const auto parts = str::split(text, ',');
    if (parts.size() < 5)
        return std::nullopt;
    FontSpec s;
    s.face = std::string(str::trim(parts[0]));
    const auto a = str::parseInt(parts[1]), b = str::parseInt(parts[2]), c = str::parseInt(parts[3]),
               d = str::parseInt(parts[4]);
    if (s.face.empty() || !a || !b || !c || !d)
        return std::nullopt;
    s.size = static_cast<int>(*a);
    s.size2 = static_cast<int>(*b);
    s.escapement = static_cast<int>(*c);
    s.weight = static_cast<int>(*d);
    return s;
}

std::shared_ptr<FontFile> FontFile::load(const std::filesystem::path& path) {
    auto bytes = file::readBinary(path);
    if (!bytes)
        return nullptr;
    std::vector<std::uint8_t> data(bytes->size());
    std::memcpy(data.data(), bytes->data(), bytes->size());
    return fromMemory(std::move(data), str::fromPath(path));
}

std::shared_ptr<FontFile> FontFile::fromMemory(std::vector<std::uint8_t> data, std::string label) {
    std::shared_ptr<FontFile> f(new FontFile());
    f->m_data = std::move(data);
    f->m_label = std::move(label);
    auto info = std::make_shared<stbtt_fontinfo>();
    const int offset = stbtt_GetFontOffsetForIndex(f->m_data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(info.get(), f->m_data.data(), offset)) {
        log::warn("font: cannot parse {}", f->m_label);
        return nullptr;
    }
    f->m_info = std::move(info);
    return f;
}

FontFile::~FontFile() = default;

bool FontFile::hasGlyph(char32_t c) const { return stbtt_FindGlyphIndex(infoOf(*this), static_cast<int>(c)) != 0; }

std::filesystem::path bundledFontPath(std::string_view fileName) {
    // Next to the executable (Windows, build tree), in the shared data
    // directory of a Unix install, or one level up (multi-config build trees).
    const auto exe = paths::executableDir();
    std::error_code ec;
    for (const auto& dir : {exe / "fonts", exe.parent_path() / "share" / "openmm2" / "fonts",
                            exe.parent_path() / "fonts" /* multi-config build trees: bin/<config>/ */}) {
        const auto p = dir / str::toPath(fileName);
        if (std::filesystem::is_regular_file(p, ec))
            return p;
    }
    return {};
}

std::shared_ptr<FontFile> findFont(std::string_view face, bool bold) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::shared_ptr<FontFile>> cache;
    const std::string key = str::lower(face) + (bold ? "|b" : "|r");
    std::lock_guard lock(mutex);
    if (auto it = cache.find(key); it != cache.end())
        return it->second;

    const FaceFiles files = filesFor(face, bold);
    std::shared_ptr<FontFile> font;
    const auto& index = systemFontIndex();
    for (const char* name : files.system) {
        if (auto it = index.find(str::lower(name)); it != index.end())
            if ((font = FontFile::load(it->second)))
                break;
    }
    if (!font) {
        if (const auto path = bundledFontPath(files.bundled); !path.empty())
            font = FontFile::load(path);
    }
    if (font)
        log::debug("font: '{}'{} -> {}", face, bold ? " bold" : "", font->label());
    else
        log::error("font: no font available for '{}' (missing {}/fonts/{})", face,
                   str::fromPath(paths::executableDir()), files.bundled);
    cache[key] = font;
    return font;
}

std::optional<FontAtlas> FontAtlas::bake(std::shared_ptr<FontFile> font, float cellHeight,
                                         const std::vector<char32_t>& extra) {
    if (!font || cellHeight <= 0.0f)
        return std::nullopt;
    const stbtt_fontinfo* info = infoOf(*font);
    FontAtlas atlas;
    atlas.m_font = font;
    atlas.m_scale = stbtt_ScaleForPixelHeight(info, cellHeight);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    atlas.m_ascent = static_cast<float>(asc) * atlas.m_scale;
    atlas.m_descent = static_cast<float>(desc) * atlas.m_scale;
    atlas.m_lineHeight = static_cast<float>(asc - desc + gap) * atlas.m_scale;

    std::vector<char32_t> codepoints;
    for (char32_t c = 0x20; c < 0x7F; ++c)
        codepoints.push_back(c);
    for (char32_t c = 0xA0; c <= 0xFF; ++c)
        codepoints.push_back(c);
    for (char32_t c : {U'‘', U'’', U'“', U'”', U'–', U'—', U'•', U'…',
                       U'€', U'™'})
        codepoints.push_back(c);
    codepoints.insert(codepoints.end(), extra.begin(), extra.end());
    std::ranges::sort(codepoints);
    codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());

    // Shelf-pack glyph bitmaps into a power-of-two atlas, growing as needed.
    struct Raster {
        char32_t c;
        int glyphIndex, x0, y0, x1, y1;
        float advance;
    };
    std::vector<Raster> rasters;
    for (char32_t c : codepoints) {
        const int gi = stbtt_FindGlyphIndex(info, static_cast<int>(c));
        if (gi == 0 && c != U' ')
            continue;
        Raster r{c, gi, 0, 0, 0, 0, 0};
        int adv, lsb;
        stbtt_GetGlyphHMetrics(info, gi, &adv, &lsb);
        r.advance = static_cast<float>(adv) * atlas.m_scale;
        stbtt_GetGlyphBitmapBox(info, gi, atlas.m_scale, atlas.m_scale, &r.x0, &r.y0, &r.x1, &r.y1);
        rasters.push_back(r);
    }

    constexpr int kPad = 1;
    int size = 128;
    while (true) {
        int x = kPad, y = kPad, rowH = 0;
        bool fits = true;
        for (const auto& r : rasters) {
            const int w = r.x1 - r.x0, h = r.y1 - r.y0;
            if (x + w + kPad > size) {
                x = kPad;
                y += rowH + kPad;
                rowH = 0;
            }
            if (y + h + kPad > size) {
                fits = false;
                break;
            }
            x += w + kPad;
            rowH = std::max(rowH, h);
        }
        if (fits || size >= 8192)
            break;
        size *= 2;
    }
    atlas.m_width = atlas.m_height = size;
    atlas.m_pixels.assign(static_cast<std::size_t>(size) * size, 0);

    int x = kPad, y = kPad, rowH = 0;
    for (const auto& r : rasters) {
        const int w = r.x1 - r.x0, h = r.y1 - r.y0;
        if (x + w + kPad > size) {
            x = kPad;
            y += rowH + kPad;
            rowH = 0;
        }
        if (y + h + kPad > size)
            break;
        Glyph g;
        g.x = static_cast<std::uint16_t>(x);
        g.y = static_cast<std::uint16_t>(y);
        g.w = static_cast<std::uint16_t>(w);
        g.h = static_cast<std::uint16_t>(h);
        g.xoff = static_cast<float>(r.x0);
        g.yoff = static_cast<float>(r.y0);
        g.advance = r.advance;
        if (w > 0 && h > 0)
            stbtt_MakeGlyphBitmap(info, atlas.m_pixels.data() + static_cast<std::size_t>(y) * size + x, w, h, size,
                                  atlas.m_scale, atlas.m_scale, r.glyphIndex);
        atlas.m_glyphs[r.c] = g;
        x += w + kPad;
        rowH = std::max(rowH, h);
    }
    return atlas;
}

const Glyph* FontAtlas::glyph(char32_t c) const {
    const auto it = m_glyphs.find(c);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

float FontAtlas::kerning(char32_t a, char32_t b) const {
    return static_cast<float>(stbtt_GetCodepointKernAdvance(infoOf(*m_font), static_cast<int>(a), static_cast<int>(b))) *
           m_scale;
}

float FontAtlas::measure(std::string_view text) const {
    return layout(text, 0.0f, 0.0f, [](const Glyph&, float, float) {});
}

std::vector<std::string> FontAtlas::wrap(std::string_view text, float maxWidth) const {
    std::vector<std::string> lines;
    std::string current;
    auto flushWord = [&](std::string& word) {
        if (word.empty())
            return;
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && measure(candidate) > maxWidth) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        word.clear();
    };
    std::string word;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool literalNewline = text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n';
        if (text[i] == '\n' || literalNewline) {
            flushWord(word);
            lines.push_back(current);
            current.clear();
            if (literalNewline)
                ++i;
        } else if (text[i] == ' ') {
            flushWord(word);
        } else {
            word.push_back(text[i]);
        }
    }
    flushWord(word);
    if (!current.empty() || lines.empty())
        lines.push_back(current);
    for (auto& l : lines)
        l = std::string(str::trim(l));
    return lines;
}

char32_t nextCodepoint(std::string_view s, std::size_t& pos) {
    const auto b0 = static_cast<unsigned char>(s[pos++]);
    if (b0 < 0x80)
        return b0;
    int extra = b0 >= 0xF0 ? 3 : b0 >= 0xE0 ? 2 : b0 >= 0xC0 ? 1 : -1;
    if (extra < 0)
        return U'�';
    char32_t c = b0 & (0x3F >> extra);
    for (int i = 0; i < extra; ++i) {
        if (pos >= s.size() || (static_cast<unsigned char>(s[pos]) & 0xC0) != 0x80)
            return U'�';
        c = (c << 6) | (static_cast<unsigned char>(s[pos++]) & 0x3F);
    }
    return c;
}

} // namespace mm2::ui
