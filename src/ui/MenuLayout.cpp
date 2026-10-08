#include "ui/MenuLayout.h"

#include "core/Log.h"

#include <cctype>

namespace mm2::ui {
namespace {

// Splits one line on `delims`, skipping empty fields like strtok does.
std::vector<std::string_view> tokens(std::string_view line, std::string_view delims) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        i = line.find_first_not_of(delims, i);
        if (i == std::string_view::npos)
            break;
        const std::size_t end = std::min(line.find_first_of(delims, i), line.size());
        out.push_back(line.substr(i, end - i));
        i = end;
    }
    return out;
}

// atoi: optional leading blanks and sign, then digits; anything else stops.
int toInt(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+'))
        negative = s[i++] == '-';
    long long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 1000000)
        v = v * 10 + (s[i++] - '0');
    return static_cast<int>(negative ? -v : v);
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view l = text.substr(start, end - start);
        if (!l.empty() && l.back() == '\r')
            l.remove_suffix(1);
        out.push_back(l);
        start = end + 1;
    }
    return out;
}

float field(const std::vector<std::string_view>& t, std::size_t i) {
    return i < t.size() ? static_cast<float>(toInt(t[i])) : 0.0f;
}

std::string_view asText(const std::vector<std::byte>& b) { return {reinterpret_cast<const char*>(b.data()), b.size()}; }

} // namespace

void MenuLayout::parseWidgets(std::string_view csv) {
    const auto all = lines(csv);
    // Columns: MENU NAME, menu id, WIDGET NAME, widget id, X, Y, W, H, DESC.
    for (std::size_t n = 1; n < all.size(); ++n) {
        const auto t = tokens(all[n], ",");
        if (t.size() < 6)
            continue;
        WidgetRow r;
        r.menuId = toInt(t[1]);
        r.name = std::string(t[2]);
        r.index = toInt(t[3]);
        r.box = {field(t, 4), field(t, 5), field(t, 6), field(t, 7)};
        m_widgets.push_back(std::move(r));
    }
}

// MenuManager::InitCommonStuff: WArray::Init (room for 300 rows; OpenMM2
// keeps every row), then WArray::Read.
MenuLayout MenuLayout::load(const vfs::Vfs& vfs) {
    MenuLayout l;
    if (auto b = vfs.readAll("tune/widget.csv"))
        l.parseWidgets(asText(*b));
    if (l.m_widgets.empty())
        log::warn("menu layout: tune/widget.csv missing or empty; using built-in positions");
    return l;
}

const MenuLayout::WidgetRow* MenuLayout::find(int menuId, int index) const {
    for (const auto& r : m_widgets)
        if (r.menuId == menuId && r.index == index)
            return &r;
    return nullptr;
}

Box MenuLayout::widget(int menuId, int index, Box code, Vec2 origin) const {
    if (const WidgetRow* r = find(menuId, index)) {
        auto pick = [](float c, float t) { return c != 0.0f && t != 0.0f ? t : c; };
        code = {pick(code.x, r->box.x), pick(code.y, r->box.y), pick(code.w, r->box.w), pick(code.h, r->box.h)};
    }
    return {code.x + origin.x, code.y + origin.y, code.w, code.h};
}

Vec2 MenuLayout::position(int menuId, int index, Vec2 code, Vec2 origin) const {
    const Box b = widget(menuId, index, {code.x, code.y, 0, 0}, origin);
    return {b.x, b.y};
}

} // namespace mm2::ui
