#pragma once

// UI v2 — FwMenuBar.
//
// Horizontal application-menu strip rendered at the top of the main
// window. Each menu is a title ("File", "Edit", …) that, on click,
// opens a context menu below itself. Uses fw2::ContextMenu's
// LayerStack-based dropdown for the actual popup — no dropdown
// rendering in this widget; it only draws the title strip and
// routes clicks.
//
// Design mirrors browser menu bars: click a title to open its menu;
// click another title while one is open and the menubar swaps to
// that menu; click outside closes (LayerStack handles the outside
// click + optional fall-through).

#include "ContextMenu.h"
#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class FwMenuBar : public Widget {
public:
    // Hydrated builder for a menu: title + its items. The items
    // vector is the same shape fw2::ContextMenu uses — use the
    // `Menu::item(…) / separator() / checkable(…)` helpers to
    // populate it.
    struct MenuDef {
        std::string label;
        std::vector<MenuEntry> items;
    };

    FwMenuBar();

    // ─── Menu population ─────────────────────────────────────────
    void addMenu(std::string label, std::vector<MenuEntry> items);
    void clearMenus();

    // ─── Query ────────────────────────────────────────────────────
    bool isOpen() const;
    void close();

    // ─── Title-bar hit test (callers used to ask v1 `contains()`) ─
    // Returns true when the pointer is inside the bar strip itself.
    // The open popup is owned by the LayerStack overlay; callers
    // should treat that as a separate concern.
    bool pointerInBar(float mx, float my) const;

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    bool onMouseDown(MouseEvent& e) override;
    bool onMouseMove(MouseMoveEvent& e) override;

public:
    // ─── Paint-side accessors ────────────────────────────────────
    // The painter (Fw2Painters.cpp) needs to see the menus + the
    // hit strips; expose read-only access. Not intended for callers.
    struct TitleStrip {
        std::string label;
        float       x = 0;
        float       w = 0;
    };
    const std::vector<TitleStrip>& titleStrips() const { return m_strips; }
    int  openIndex()  const { return m_openIndex; }
    int  hoverIndex() const { return m_hoverIndex; }
    float titleBarHeight() const { return bounds().h; }

private:
    // Measure a title's width using the theme's font size + a small
    // padding on each side. Rebuilt lazily in onMeasure / paint.
    void recomputeStrips(UIContext& ctx);
    int  hitTest(float mx, float my) const;

    // Open menu i at its title position. Closes any previously-
    // open menu first.
    void openAt(int i);

    std::vector<MenuDef>    m_menus;
    std::vector<TitleStrip> m_strips;
    int                     m_openIndex  = -1;
    int                     m_hoverIndex = -1;

    // Cached font size the strips were built for. Rebuild if theme
    // changes beneath us.
    float m_stripFontSize = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
