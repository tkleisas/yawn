#include "MenuBar.h"
#include "UIContext.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

FwMenuBar::FwMenuBar() {
    setSizePolicy(SizePolicy::fixed());
    setRelayoutBoundary(true);
    setFocusable(false);
    setClickOnly(true);   // no drag behaviour
}

// ───────────────────────────────────────────────────────────────────
// Menu population
// ───────────────────────────────────────────────────────────────────

void FwMenuBar::addMenu(std::string label, std::vector<MenuEntry> items) {
    m_menus.push_back({std::move(label), std::move(items)});
    m_stripFontSize = 0.0f;   // force strip recompute
    invalidate();
}

void FwMenuBar::clearMenus() {
    m_menus.clear();
    m_strips.clear();
    m_openIndex  = -1;
    m_hoverIndex = -1;
    invalidate();
}

bool FwMenuBar::isOpen() const {
    // A menu we opened is still visible on the context-menu stack.
    // Checking the manager's isOpen() isn't quite right because any
    // context menu (right-click, etc.) would count; but since the
    // v2 ContextMenuManager is a singleton that can only show one
    // stack at a time, and we track m_openIndex ourselves, the
    // local flag is authoritative.
    return m_openIndex >= 0 && ContextMenuManager::instance().isOpen();
}

void FwMenuBar::close() {
    if (ContextMenuManager::instance().isOpen())
        ContextMenuManager::instance().close();
    m_openIndex = -1;
}

bool FwMenuBar::pointerInBar(float mx, float my) const {
    const Rect& b = bounds();
    return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
}

// ───────────────────────────────────────────────────────────────────
// Measure
// ───────────────────────────────────────────────────────────────────

Size FwMenuBar::onMeasure(Constraints c, UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    recomputeStrips(ctx);
    float w = std::max(c.minW, c.maxW);   // fill full width
    // Bar height = control height (matches DropDown / Button)
    float h = std::max(m.controlHeight, 24.0f);
    w = std::min(w, c.maxW);
    h = std::max(h, c.minH);
    h = std::min(h, c.maxH);
    return {w, h};
}

void FwMenuBar::recomputeStrips(UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float fs = m.fontSize;
    const float padX = m.baseUnit * 1.5f;

    // Cache check — recompute only when the underlying font size or
    // menu list changes.
    if (m_stripFontSize == fs && m_strips.size() == m_menus.size())
        return;

    m_strips.clear();
    m_strips.reserve(m_menus.size());
    float x = m.baseUnit;
    for (const auto& menu : m_menus) {
        float tw = ctx.textMetrics
            ? ctx.textMetrics->textWidth(menu.label, fs)
            : static_cast<float>(menu.label.size()) * fs * 0.55f;
        float cellW = tw + padX * 2.0f;
        m_strips.push_back({menu.label, x, cellW});
        x += cellW;
    }
    m_stripFontSize = fs;
}

// ───────────────────────────────────────────────────────────────────
// Event handling
// ───────────────────────────────────────────────────────────────────

int FwMenuBar::hitTest(float mx, float my) const {
    if (!pointerInBar(mx, my)) return -1;
    const Rect& b = bounds();
    const float localX = mx - b.x;
    for (int i = 0; i < static_cast<int>(m_strips.size()); ++i) {
        const auto& s = m_strips[i];
        if (localX >= s.x && localX < s.x + s.w) return i;
    }
    return -1;
}

bool FwMenuBar::onMouseDown(MouseEvent& e) {
    if (!m_enabled) return false;
    if (e.button != MouseButton::Left) return false;
    const int i = hitTest(e.x, e.y);
    if (i < 0) return false;
    // Re-click on the same open title closes the menu. Otherwise
    // open (switches if another is already open).
    if (i == m_openIndex) {
        close();
    } else {
        openAt(i);
    }
    return true;
}

bool FwMenuBar::onMouseMove(MouseMoveEvent& e) {
    const int hit = hitTest(e.x, e.y);
    if (hit != m_hoverIndex) {
        m_hoverIndex = hit;
        // If a menu is already open and the user hovers onto another
        // title, switch to that menu — standard menubar affordance.
        // Skip when hover leaves the bar entirely (hit == -1); let
        // the user commit to clicking elsewhere to dismiss.
        if (m_openIndex >= 0 && hit >= 0 && hit != m_openIndex) {
            openAt(hit);
        }
    }
    return false;
}

void FwMenuBar::openAt(int i) {
    if (i < 0 || i >= static_cast<int>(m_menus.size())) return;
    // Compute the screen position: just below the title strip.
    const Rect& b = bounds();
    const auto& s = m_strips[i];
    const Point screenPos{ b.x + s.x, b.y + b.h };
    // ContextMenu::show closes any existing menu and opens this one.
    ContextMenuManager::instance().show(m_menus[i].items, screenPos);
    m_openIndex = i;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
