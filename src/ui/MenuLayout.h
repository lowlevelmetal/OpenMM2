#pragma once

// Widget rectangles from tune/widget.csv.
//
// MM2's frontend reads the table when it starts (MM2
// `MenuManager::InitCommonStuff` -> `WArray::Read`). Each widget a menu
// creates is looked up by the menu's id and the widget's creation index
// (0-based, in the order the menu adds its widgets), and every non-zero
// component of the rectangle the code asked for is replaced by the table's
// non-zero value (`WArray::RetrieveWidgetData`). Widgets inside dialogs are
// relative to the dialog, which is centred on the screen at the size of its
// picture (`PUMenuBase::PUMenuBase`). tune/menu.csv is also loaded by the
// game (`MArray::Read`) but never consulted.
//
// The table is parsed the way the game parses it: comma separated with empty
// fields skipped, numbers read like atoi ("4 State" is 4), the first row for
// a widget wins. Rectangles are in the 640x480 menu space.

#include "ui/Widgets.h"
#include "vfs/Vfs.h"

#include <string>
#include <string_view>
#include <vector>

namespace mm2::ui {

class MenuLayout {
public:
    struct WidgetRow {
        int menuId = 0;
        int index = 0;
        std::string name; // the WIDGET NAME column (informational)
        Box box;
    };

    // Parses tune/widget.csv text (header row first).
    void parseWidgets(std::string_view csv);
    // Loads tune/widget.csv from the game data; a missing file leaves it empty.
    static MenuLayout load(const vfs::Vfs& vfs);

    // The table's row for widget `index` of menu `menuId`, if listed.
    const WidgetRow* find(int menuId, int index) const;
    // `code` with each non-zero component replaced by the table's non-zero
    // value (MM2 `WArray::RetrieveWidgetData`), offset by `origin` (the
    // dialog's top-left corner for widgets inside dialogs).
    Box widget(int menuId, int index, Box code, Vec2 origin = {}) const;
    // Shorthand for sprite buttons and lamps: the code passes only a position.
    Vec2 position(int menuId, int index, Vec2 code, Vec2 origin = {}) const;

    const std::vector<WidgetRow>& widgets() const { return m_widgets; }

private:
    std::vector<WidgetRow> m_widgets;
};

// Top-left corner of a dialog picture of the given size, centred on the
// 640x480 screen (MM2 `PUMenuBase::PUMenuBase`).
inline Vec2 dialogOrigin(Vec2 size) { return {(640.0f - size.x) * 0.5f, (480.0f - size.y) * 0.5f}; }

} // namespace mm2::ui
