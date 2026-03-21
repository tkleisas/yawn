#pragma once
// Panel wrapper widgets — thin Widget subclasses that delegate to existing
// panel classes for rendering and events.  Only MenuBarWrapper remains;
// other panels have been migrated to full fw::Widget subclasses.

#include "Widget.h"
#include "../MenuBar.h"

namespace yawn {
namespace ui {
namespace fw {

// ─── MenuBarWrapper ──────────────────────────────────────────────────────
// Layout-only wrapper.  paint() is a no-op because the MenuBar must be
// rendered last (on top of other panels) so its dropdown menus aren't
// occluded.  App::render() calls MenuBar::render() manually after the tree.

class MenuBarWrapper : public Widget {
public:
    explicit MenuBarWrapper(MenuBar& bar) : m_bar(bar) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_bar.height()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext&) override { /* rendered manually by App */ }

private:
    MenuBar& m_bar;
};

} // namespace fw
} // namespace ui
} // namespace yawn
